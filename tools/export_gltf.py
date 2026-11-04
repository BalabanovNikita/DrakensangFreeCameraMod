#!/usr/bin/env python3
"""Export Drakensang (2008) levels or single models to glTF 2.0 (.glb) with textures.

  export_gltf.py --level location09 -o murolosh.glb          a whole level (static objects)
  export_gltf.py --model _location_09_grounds/dungeon_murolosch_tempel      one model (gfxlib resource)
  options: --effects (also water/fog/particles-free effects meshes), --all-lods, --max-texture 1024,
           --no-lightmaps, --props (doors, chests, barrels, state objects too; default on for levels)

Reads the game's NPK archives directly: Nebula2 binary scene graphs (.n2), nvx2 meshes and DDS
textures. Objects are placed with their database transforms; transforms of scene nodes (position,
quaternion, scale, pivots) are applied; of every LOD node only the most detailed child is used.
Units are metres, Y up (Nebula2 uses the same right-handed frame as glTF).

Each glTF material carries "extras": {"shader", "textures", "kind"} - used by gltf_to_vintagestory.py
to choose block families (stone, wood, water...).
"""
import argparse
import io
import json
import math
import os
import re
import sqlite3
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_paths import need_game  # noqa: E402
from gen_extras import toc, PACKAGES, D1_DIR  # noqa: E402

PROP_TABLES = ['_Instance_DestroyableEntity', '_Instance_Door', '_Instance_Chest', '_Instance_StateObject',
               '_Instance_WorkBench', '_Instance_Object', '_Instance_BookStand']
SKIP_RES = ('levelfx/', 'effects/', 'dummies/', 'characters/', 'items_', 'skies/', 'cursors/', 'gui/')
SKIP_SHADERS = {'particle2', 'particle2_additive', 'volumefog', 'skybox', 'mattepainting', 'environment',
                'skinned', 'skinned_alpha', 'shadow'}
EFFECT_SHADERS = {'volumefog', 'mattepainting'}           # exported with --effects
SKIP_CLASSES = {'nparticleshapenode', 'nparticleshapenode2', 'nskinshapenode', 'nshadownode', 'nshadowskinshapenode'}
# nvx2 vertex components of the late Nebula2 used by Drakensang (same table as Nebula3's N2 reader):
# (name, bit, byte size, decoder)
F3, F2, F4 = ('f', 3), ('f', 2), ('f', 4)
COMPONENTS = [('coord', 1 << 0, 12, F3), ('normal', 1 << 1, 12, F3), ('normal', 1 << 2, 4, ('b4n', 3)),
              ('uv0', 1 << 3, 8, F2), ('uv0', 1 << 4, 4, ('s2', 2)), ('uv1', 1 << 5, 8, F2), ('uv1', 1 << 6, 4, ('s2', 2)),
              ('uv2', 1 << 7, 8, F2), ('uv2', 1 << 8, 4, ('s2', 2)), ('uv3', 1 << 9, 8, F2), ('uv3', 1 << 10, 4, ('s2', 2)),
              ('color', 1 << 11, 16, F4), ('color', 1 << 12, 4, ('ub4n', 4)), ('tangent', 1 << 13, 12, F3),
              ('tangent', 1 << 14, 4, ('b4n', 3)), ('binormal', 1 << 15, 12, F3), ('binormal', 1 << 16, 4, ('b4n', 3)),
              ('weights', 1 << 17, 16, F4), ('weights', 1 << 18, 4, ('ub4n', 4)), ('jindices', 1 << 19, 16, F4),
              ('jindices', 1 << 20, 4, ('ub4', 4))]


# ---- archives -----------------------------------------------------------------------------------
class Archives:
    def __init__(self, game_dir):
        self.files = {}
        for p in PACKAGES:
            path = os.path.join(game_dir, p)
            if os.path.exists(path):
                for k, v in toc(path).items():
                    self.files.setdefault(k, v)
        self.fh = {}

    def has(self, name):
        return name.lower() in self.files

    def read(self, name):
        path, off, size = self.files[name.lower()]
        f = self.fh.setdefault(path, open(path, 'rb'))
        f.seek(off)
        return f.read(size)


def res_path(assign_path):
    """'meshes:a/b.nvx2' -> 'export/meshes/a/b.nvx2'"""
    m = re.match(r'(\w+):(.*)', assign_path)
    return ('export/%s/%s' % (m.group(1), m.group(2))).lower() if m else assign_path.lower()


# ---- math ---------------------------------------------------------------------------------------
def mat_identity():
    return np.identity(4, dtype=np.float64)


def mat_translate(t):
    m = mat_identity()
    m[3, :3] = t
    return m


def mat_scale(s):
    return np.diag([s[0], s[1], s[2], 1.0])


def mat_quat(q):
    x, y, z, w = q
    # row-vector convention (v * M), as Nebula2's matrix44
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0],
                     [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0],
                     [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0],
                     [0, 0, 0, 1]], dtype=np.float64)


def mat_euler(e):
    """Nebula2 euler (x, y, z radians), applied x then y then z."""
    cx, sx, cy, sy, cz, sz = math.cos(e[0]), math.sin(e[0]), math.cos(e[1]), math.sin(e[1]), math.cos(e[2]), math.sin(e[2])
    rx = np.array([[1, 0, 0, 0], [0, cx, sx, 0], [0, -sx, cx, 0], [0, 0, 0, 1]])
    ry = np.array([[cy, 0, -sy, 0], [0, 1, 0, 0], [sy, 0, cy, 0], [0, 0, 0, 1]])
    rz = np.array([[cz, sz, 0, 0], [-sz, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])
    return rx @ ry @ rz


class Node:
    def __init__(self, cls, name, parent):
        self.cls, self.name, self.parent, self.children = cls, name, parent, []
        self.pos, self.scale, self.quat, self.euler = (0, 0, 0), (1, 1, 1), None, None
        self.rpivot, self.spivot = (0, 0, 0), (0, 0, 0)
        self.textures, self.ints, self.shader, self.mesh, self.group = {}, {}, None, None, 0

    def local(self):
        m = mat_translate([-v for v in self.spivot]) @ mat_scale(self.scale) @ mat_translate(self.spivot)
        rot = mat_quat(self.quat) if self.quat else mat_euler(self.euler) if self.euler else mat_identity()
        m = m @ mat_translate([-v for v in self.rpivot]) @ rot @ mat_translate(self.rpivot)
        return m @ mat_translate(self.pos)


def parse_n2(data):
    """Root Node of a Nebula2 binary scene script."""
    hl = struct.unpack_from('<H', data, 4)[0]
    p = 6 + hl
    root = Node('root', 'root', None)
    cur = root

    def string(a, o=0):
        n = struct.unpack_from('<H', a, o)[0]
        return a[o + 2:o + 2 + n].decode('latin1'), o + 2 + n
    while p + 4 <= len(data):
        fcc = data[p:p + 4][::-1].decode('latin1')
        if fcc == '_new':
            l1 = struct.unpack_from('<H', data, p + 4)[0]
            cls = data[p + 6:p + 6 + l1].decode('latin1')
            l2 = struct.unpack_from('<H', data, p + 6 + l1)[0]
            name = data[p + 8 + l1:p + 8 + l1 + l2].decode('latin1')
            node = Node(cls, name, cur)
            cur.children.append(node)
            cur = node
            p += 8 + l1 + l2
            continue
        ln = struct.unpack_from('<H', data, p + 4)[0]
        a = data[p + 6:p + 6 + ln]
        p += 6 + ln
        if fcc == '_sel':
            if a.startswith(b'..') and cur.parent:
                cur = cur.parent
        elif fcc == 'SPOS':
            cur.pos = struct.unpack('<3f', a)
        elif fcc == 'SQUT':
            cur.quat = struct.unpack('<4f', a)
        elif fcc == 'SEUL':
            cur.euler = struct.unpack('<3f', a)
        elif fcc == 'SSCL':
            cur.scale = struct.unpack('<3f', a)
        elif fcc == 'SRTP':
            cur.rpivot = struct.unpack('<3f', a)
        elif fcc == 'SSCP':
            cur.spivot = struct.unpack('<3f', a)
        elif fcc == 'STXT':
            k, o = string(a)
            v, _ = string(a, o)
            cur.textures[k] = v
        elif fcc == 'SINT':
            k, o = string(a)
            cur.ints[k] = struct.unpack_from('<i', a, o)[0]
        elif fcc == 'SSHD':
            cur.shader, _ = string(a)
        elif fcc == 'SMSH':
            cur.mesh, _ = string(a)
        elif fcc == 'SGRI':
            cur.group = struct.unpack('<i', a)[0]
        elif fcc == 'STUS' and len(a) == 8:      # multilayer: texture uv scale of layer i
            i, v = struct.unpack('<if', a)
            cur.ints['uvscale%d' % i] = v
    return root


def shapes(node, parent_m, all_lods, out):
    """(world-of-scene matrix, shape node) for every renderable shape below node."""
    m = node.local() @ parent_m if node.cls != 'root' else parent_m
    if node.cls in SKIP_CLASSES:
        return out
    if node.mesh and node.shader:
        out.append((m, node))
    kids = node.children
    if node.cls == 'nlodnode' and not all_lods and kids:
        kids = kids[:1]
    for c in kids:
        shapes(c, m, all_lods, out)
    return out


# ---- meshes -------------------------------------------------------------------------------------
class Mesh:
    def __init__(self, data):
        _, ng, nv, width, nt, ne, mask = struct.unpack_from('<4s6i', data, 0)
        self.groups = [struct.unpack_from('<6i', data, 28 + i * 24) for i in range(ng)]
        base = 28 + ng * 24
        stride = width * 4
        raw = np.frombuffer(data, dtype=np.uint8, count=nv * stride, offset=base).reshape(nv, stride)
        off, self.comp = 0, {}
        for name, bit, size, (kind, n) in COMPONENTS:
            if not mask & bit:
                continue
            chunk = np.ascontiguousarray(raw[:, off:off + size])
            if kind == 'f':
                v = chunk.view('<f4').reshape(nv, n).astype(np.float64)
            elif kind == 's2':      # 2 shorts; the shaders unpack them with 1/8192
                v = chunk.view('<i2').reshape(nv, 2).astype(np.float64) / 8192.0
            elif kind == 'b4n':     # 4 unsigned bytes normalised to -1..1
                v = chunk.astype(np.float64)[:, :n] / 255.0 * 2.0 - 1.0
            elif kind == 'ub4n':
                v = chunk.astype(np.float64)[:, :n] / 255.0
            else:
                v = chunk.astype(np.float64)[:, :n]
            self.comp.setdefault(name, v)
            off += size
        if off != stride:
            raise ValueError('vertex layout mismatch (mask %x, stride %d, parsed %d)' % (mask, stride, off))
        self.indices = np.frombuffer(data, dtype='<u2', count=nt * 3, offset=base + nv * width * 4).reshape(nt, 3)

    def group(self, gi):
        fv, nv, ft, ntr, _, _ = self.groups[gi]
        idx = self.indices[ft:ft + ntr].astype(np.int64)
        # indices are usually absolute in the vertex buffer; take the group's vertex range
        if len(idx) and idx.min() >= fv and idx.max() < fv + nv:
            idx = idx - fv
        elif len(idx) and idx.max() >= nv:
            raise ValueError('group %d: indices %d..%d outside vertices %d+%d' % (gi, idx.min(), idx.max(), fv, nv))
        sel = {k: v[fv:fv + nv] for k, v in self.comp.items()}
        return sel, idx


# ---- textures -----------------------------------------------------------------------------------
class Textures:
    def __init__(self, arc, max_size):
        self.arc, self.max_size, self.cache = arc, max_size, {}

    def get(self, assign_path):
        """(png/jpeg bytes, mime, has_alpha, mean rgb) or None"""
        if assign_path in self.cache:
            return self.cache[assign_path]
        name = res_path(assign_path)
        res = None
        if self.arc.has(name):
            try:
                img = Image.open(io.BytesIO(self.arc.read(name)))
                img.load()
                if self.max_size and max(img.size) > self.max_size:
                    s = self.max_size / max(img.size)
                    img = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.LANCZOS)
                alpha = img.mode in ('RGBA', 'LA') and img.getchannel('A').getextrema()[0] < 250
                buf = io.BytesIO()
                if alpha:
                    img.convert('RGBA').save(buf, 'PNG', optimize=True)
                    mime = 'image/png'
                else:
                    img.convert('RGB').save(buf, 'JPEG', quality=90)
                    mime = 'image/jpeg'
                res = (buf.getvalue(), mime, alpha)
            except Exception as e:
                print('  texture %s: %s' % (assign_path, e))
        self.cache[assign_path] = res
        return res


def bake_multilayer(arc, textures, ints, size=2048):
    """Colour texture of a multilayer surface: the DiffMap0 mask (RGBA = weights of layers 0..3)
    blended with the layer textures NoiseMap0..3, each tiled with its uv scale."""
    def load(path, mode='RGBA'):
        name = res_path(path)
        return Image.open(io.BytesIO(arc.read(name))).convert(mode) if path and arc.has(name) else None
    mask = load(textures.get('DiffMap0'))
    if mask is None:
        return None
    m = np.asarray(mask.resize((size, size), Image.BILINEAR), dtype=np.float64) / 255.0
    w = m / np.maximum(m.sum(axis=2, keepdims=True), 1e-6)
    out = np.zeros((size, size, 3))
    yy, xx = np.mgrid[0:size, 0:size]
    for k in range(4):
        layer = load(textures.get('NoiseMap%d' % k), 'RGB')
        if layer is None or w[..., k].max() < 0.01:
            continue
        tiles = ints.get('uvscale%d' % k, 1.0) or 1.0
        res = max(4, min(layer.width, int(round(size / tiles))))   # texel footprint of the baked texture
        lay = np.asarray(layer.resize((res, res), Image.BOX), dtype=np.float64)
        u = ((xx / size) * tiles * res).astype(np.int64) % res
        v = ((yy / size) * tiles * res).astype(np.int64) % res
        out += w[..., k:k + 1] * lay[v, u]
    buf = io.BytesIO()
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(buf, 'JPEG', quality=90)
    return buf.getvalue(), 'image/jpeg', False


# ---- glTF writer --------------------------------------------------------------------------------
class Gltf:
    def __init__(self):
        self.j = {'asset': {'version': '2.0', 'generator': 'drakensang _mod/tools/export_gltf.py'},
                  'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'materials': [],
                  'textures': [], 'images': [], 'samplers': [{'magFilter': 9729, 'minFilter': 9987,
                                                              'wrapS': 10497, 'wrapT': 10497}],
                  'accessors': [], 'bufferViews': [], 'buffers': []}
        self.bin = bytearray()
        self.images = {}

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)}
        if target:
            v['target'] = target
        self.bin += data
        self.j['bufferViews'].append(v)
        return len(self.j['bufferViews']) - 1

    def accessor(self, arr, ctype, typ, target, minmax=False):
        a = {'bufferView': self.view(arr.tobytes(), target), 'componentType': ctype, 'count': int(arr.shape[0]),
             'type': typ}
        if minmax:
            a['min'] = [float(x) for x in arr.min(axis=0)]
            a['max'] = [float(x) for x in arr.max(axis=0)]
        self.j['accessors'].append(a)
        return len(self.j['accessors']) - 1

    def texture(self, key, tex):
        if key not in self.images:
            data, mime, _ = tex
            self.j['images'].append({'bufferView': self.view(data), 'mimeType': mime, 'name': key})
            self.j['textures'].append({'sampler': 0, 'source': len(self.j['images']) - 1})
            self.images[key] = len(self.j['textures']) - 1
        return self.images[key]

    def write(self, path):
        self.j['buffers'] = [{'byteLength': len(self.bin)}]
        js = json.dumps(self.j, separators=(',', ':')).encode()
        js += b' ' * ((4 - len(js) % 4) % 4)
        while len(self.bin) % 4:
            self.bin.append(0)
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin)))
            f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
            f.write(struct.pack('<II', len(self.bin), 0x004E4942) + bytes(self.bin))


def kind_of(shader, textures):
    t = ' '.join(textures.values()).lower()
    if shader == 'simpleriver' or 'wasser' in t:
        return 'water'
    if shader in EFFECT_SHADERS:
        return 'effect'
    if shader in ('alpha', 'static_atest', 'lightmapped_alpha'):
        return 'alpha'
    return 'solid'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--level', action='append', default=[], help='level id (repeatable), e.g. location09')
    ap.add_argument('--model', action='append', default=[], help='gfxlib resource, e.g. _location_09_grounds/dungeon_murolosch_tempel')
    ap.add_argument('-o', '--output', help='output .glb')
    ap.add_argument('--game', default=D1_DIR, help='game directory (default: DRAKENSANG_DIR)')
    ap.add_argument('--effects', action='store_true', help='include fog volumes and matte paintings')
    ap.add_argument('--no-props', action='store_true', help='levels: only the static environment')
    ap.add_argument('--all-lods', action='store_true', help='all LOD levels instead of the most detailed one')
    ap.add_argument('--no-lightmaps', action='store_true', help='do not attach lightmaps (occlusion, UV set 1)')
    ap.add_argument('--max-texture', type=int, default=1024, help='downscale larger textures (0 = keep)')
    args = ap.parse_args()
    if not args.level and not args.model:
        ap.error('give --level and/or --model')
    out = args.output or ((args.level or [os.path.basename(args.model[0])])[0] + '.glb')

    need_game(args.game)
    arc = Archives(args.game)
    texs = Textures(arc, args.max_texture)
    objects = [(r, np.identity(4)) for r in args.model]
    if args.level:
        db = sqlite3.connect('file:%s/export/db/game.db4?mode=ro' % args.game, uri=True)
        db.text_factory = lambda b: b.decode('cp1252', 'replace')
        tables = ['_Instance__Environment'] + ([] if args.no_props else PROP_TABLES)
        for lvl in args.level:
            n0 = len(objects)
            for t in tables:
                for res, tr in db.execute('select Graphics, Transform from "%s" where _Level=? and Graphics<>\'\'' % t,
                                          (lvl,)):
                    if res.lower().startswith(SKIP_RES):
                        continue
                    objects.append((res, np.array(struct.unpack('<16f', tr), dtype=np.float64).reshape(4, 4)))
            print('%s: %d objects' % (lvl, len(objects) - n0))

    # gather geometry per material
    scenes, meshes, mats = {}, {}, {}
    skipped = {}
    for res, objm in objects:
        if res not in scenes:
            key = 'export/gfxlib/%s.n2' % res.lower()
            scenes[res] = shapes(parse_n2(arc.read(key)), mat_identity(), args.all_lods, []) if arc.has(key) else None
            if scenes[res] is None:
                print('  missing scene', res)
        for m, node in scenes[res] or []:
            sh = node.shader
            if sh in SKIP_SHADERS and not (args.effects and sh in EFFECT_SHADERS):
                skipped[sh] = skipped.get(sh, 0) + 1
                continue
            mk = res_path(node.mesh)
            if mk not in meshes:
                try:
                    meshes[mk] = Mesh(arc.read(mk)) if arc.has(mk) else None
                except Exception as e:
                    print('  mesh %s: %s' % (mk, e))
                    meshes[mk] = None
            mesh = meshes[mk]
            if not mesh or node.group >= len(mesh.groups):
                continue
            comp, idx = mesh.group(node.group)
            if 'coord' not in comp or not len(idx):
                continue
            world = m @ objm
            pos = np.hstack([comp['coord'], np.ones((len(comp['coord']), 1))]) @ world
            pos = pos[:, :3]
            nrm = None
            if 'normal' in comp:
                inv = np.linalg.inv(world[:3, :3]).T
                nrm = comp['normal'] @ inv
                ln = np.linalg.norm(nrm, axis=1, keepdims=True)
                nrm = nrm / np.where(ln > 1e-9, ln, 1)
            # winding: make triangle normals agree with vertex normals (glTF front faces are CCW)
            tri = pos[idx]
            fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
            if nrm is not None and np.einsum('ij,ij->', fn, nrm[idx].sum(axis=1)) < 0:
                idx = idx[:, ::-1]
            elif nrm is None and np.linalg.det(world[:3, :3]) < 0:
                idx = idx[:, ::-1]
            textures = dict(node.textures)
            diff = textures.get('DiffMap0', '')
            light = textures.get('DiffMap1', '') if sh in ('lightmapped2', 'lightmapped', 'multilayeredbump',
                                                         'lightmapped_alpha') else ''
            cull = node.ints.get('CullMode', 2)
            mkey = (diff, light, sh, cull)
            entry = mats.setdefault(mkey, {'pos': [], 'nrm': [], 'uv0': [], 'uv1': [], 'idx': [], 'n': 0,
                                           'textures': textures, 'shader': sh, 'ints': dict(node.ints)})
            entry['pos'].append(pos.astype(np.float32))
            entry['nrm'].append((nrm if nrm is not None else np.zeros_like(pos)).astype(np.float32))
            entry['uv0'].append(comp.get('uv0', np.zeros((len(pos), 2))).astype(np.float32))
            entry['uv1'].append(comp.get('uv1', comp.get('uv0', np.zeros((len(pos), 2)))).astype(np.float32))
            entry['idx'].append((idx + entry['n']).astype(np.uint32))
            entry['n'] += len(pos)
    if skipped:
        print('skipped shaders:', ', '.join('%s %d' % kv for kv in sorted(skipped.items())))

    g = Gltf()
    prims, ntri = [], 0
    for (diff, light, sh, cull), e in sorted(mats.items(), key=lambda kv: kv[0]):
        pos = np.concatenate(e['pos'])
        idx = np.concatenate(e['idx']).reshape(-1)
        attrs = {'POSITION': g.accessor(pos, 5126, 'VEC3', 34962, True),
                 'NORMAL': g.accessor(np.concatenate(e['nrm']), 5126, 'VEC3', 34962),
                 'TEXCOORD_0': g.accessor(np.concatenate(e['uv0']), 5126, 'VEC2', 34962)}
        mat = {'name': os.path.basename(diff.split(':')[-1]) or sh, 'doubleSided': cull == 1 or sh != 'lightmapped2',
               'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 0.9},
               'extras': {'shader': sh, 'kind': kind_of(sh, e['textures']), 'textures': e['textures']}}
        if sh.startswith('multilayer'):
            if diff not in texs.cache:
                texs.cache['baked:' + diff] = bake_multilayer(arc, e['textures'], e['ints'])
            t = texs.cache.get('baked:' + diff)
            diff = 'baked:' + diff
            mat['extras']['baked'] = 'multilayer'
        else:
            t = texs.get(diff) if diff else None
        if t:
            mat['pbrMetallicRoughness']['baseColorTexture'] = {'index': g.texture(diff, t)}
            if t[2]:
                mat['alphaMode'] = 'BLEND' if mat['extras']['kind'] in ('water', 'effect') else 'MASK'
        if mat['extras']['kind'] == 'water':
            mat['pbrMetallicRoughness']['baseColorFactor'] = [0.75, 0.85, 1.0, 0.75]
            mat['alphaMode'] = 'BLEND'
        if light and not args.no_lightmaps:
            lt = texs.get(light)
            if lt:
                attrs['TEXCOORD_1'] = g.accessor(np.concatenate(e['uv1']), 5126, 'VEC2', 34962)
                mat['occlusionTexture'] = {'index': g.texture(light, lt), 'texCoord': 1}
        g.j['materials'].append(mat)
        prims.append({'attributes': attrs, 'indices': g.accessor(idx, 5125, 'SCALAR', 34963),
                      'material': len(g.j['materials']) - 1, 'mode': 4})
        ntri += len(idx) // 3
    g.j['meshes'].append({'name': os.path.splitext(os.path.basename(out))[0], 'primitives': prims})
    g.j['nodes'].append({'name': os.path.splitext(os.path.basename(out))[0], 'mesh': 0})
    g.j['scenes'][0]['nodes'] = [0]
    for k in ('textures', 'images'):
        if not g.j[k]:
            del g.j[k]
    g.write(out)
    print('wrote %s: %d materials, %d triangles, %d textures, %.1f MB' % (
        out, len(prims), ntri, len(g.images), os.path.getsize(out) / 1e6))


if __name__ == '__main__':
    sys.exit(main())
