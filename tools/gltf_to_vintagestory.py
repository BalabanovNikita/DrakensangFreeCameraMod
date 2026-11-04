#!/usr/bin/env python3
"""Convert a glTF 2.0 model (.glb) into Vintage Story WorldEdit schematics (blocks).

  gltf_to_vintagestory.py murolosh.glb --scale 1 -o murolosh
      -> murolosh.json (or murolosh_x0_z0.json ... tiles if larger than --tile), murolosh_preview.png

Surfaces are sampled densely (textures included), every sample marks the block it falls into; a
block takes the average colour of its samples and the material family most of them vote for
(wood / stone / brick / plaster / water ..., from the material names, see FAMILY_HINTS and the
"extras" written by export_gltf.py). The block type is the palette entry of that family with the
nearest colour (CIE Lab). The palette comes from the installed game: block codes and the average
colour of their textures (built once into vs_palette.json next to this script; edit it to remove
or add blocks).

Options: --scale blocks per metre (1 = 1 block per metre), --thicken N (grow walls by N blocks),
--no-water, --families stone,wood (restrict), --tile 1024 (schematic size limit is 1024).
Import in game: copy the .json files to VintagestoryData/WorldEdit/, then
  /we import <name>      (loads the schematic into the clipboard)
  /we paste              (places it at your position / the selected start)
"""
import argparse
import colorsys
import glob
import io
import json
import math
import os
import re
import struct
import sys
import zlib

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
VS_ASSETS = '/opt/vintagestory/assets'
PALETTE = os.path.join(HERE, 'vs_palette.json')

# material name / texture path keywords -> family (German names of Drakensang's assets included)
FAMILY_HINTS = [
    ('water', r'wasser|water|river|fluss|see_'),
    ('wood', r'holz|wood|brett|balken|plank|planke|bohle|tisch|fass|kiste|truhe|steg|kran|wagen|rad_|leiter|'
             r'dachstuhl|zaun|tuer|tor_holz|regal|bank|stuhl|korb|moebel|schrank'),
    ('brick', r'ziegel|backstein|brick'),
    ('roof', r'dach|schindel|roof'),
    ('plaster', r'putz|plaster|lehm|fachwerk_wand'),
    ('metal', r'metall|metal|eisen|iron|gold|bronze|kupfer|messing|kette'),
    ('stone', r'stein|fels|rock|stone|wand|mauer|boden|ground|gewoelbe|stollen|hoehle|cave|statue|saeule|'
              r'tempel|pfeiler|treppe|bruecke|marmor|granit|murolosch'),
]
# which palette families may be used for each material family
FAMILY_BLOCKS = {'stone': ['stone'], 'wood': ['wood'], 'brick': ['brick', 'stone'], 'roof': ['tile', 'brick', 'wood'],
                 'plaster': ['plaster', 'stone'], 'metal': ['stone', 'brick'], 'water': ['water'],
                 'any': ['stone', 'wood', 'brick', 'plaster', 'tile']}


# ---- palette --------------------------------------------------------------------------------------
def json5(text):
    text = re.sub(r'//[^\n]*', '', text)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'([{,\[]\s*)([A-Za-z_@*$][\w\-@*$]*)\s*:', r'\1"\2":', text)
    text = re.sub(r',(\s*[}\]])', r'\1', text)
    return json.loads(text)


def tex_color(assets, base):
    """Average sRGB colour of textures/<base>.png (or <base>1..n.png for 'name*')."""
    root = os.path.join(assets, 'survival', 'textures')
    paths = sorted(glob.glob(os.path.join(root, base.replace('*', '') + ('[0-9]*.png' if '*' in base else '.png'))))
    if not paths and '*' in base:
        paths = glob.glob(os.path.join(root, base.replace('*', '') + '.png'))
    cols = []
    for p in paths[:6]:
        a = np.asarray(Image.open(p).convert('RGBA'), dtype=np.float64)
        w = a[..., 3:4] / 255.0
        if w.sum() < 1:
            continue
        cols.append((a[..., :3] * w).sum(axis=(0, 1)) / w.sum())
    return [round(float(v), 1) for v in np.mean(cols, axis=0)] if cols else None


def build_palette(assets):
    props = lambda name: [v.get('code') or v.get('Code') for v in json5(open(os.path.join(
        assets, 'survival', 'worldproperties', 'block', name + '.json')).read())['variants']]
    rocks, woods = props('rockwithdeposit'), props('wood')
    entries = []

    def add(code, family, base):
        c = tex_color(assets, base)
        if c:
            entries.append({'code': code, 'family': family, 'rgb': c, 'enabled': True})
    for r in rocks:
        add('rock-%s' % r, 'stone', 'block/stone/rock/%s*' % r)
        add('cobblestone-%s' % r, 'stone', 'block/stone/cobblestone/%s*' % r)
    for r in ['whitemarble', 'greenmarble', 'redmarble', 'suevite', 'kimberlite']:
        add('rockpolished-%s' % r, 'stone', 'block/stone/polishedrock/%s' % r)
    for r in ['suevite', 'travertine']:
        add('stonebricks-%s' % r, 'stone', 'block/stone/brick/%s*' % r)
    for w in woods:
        add('planks-%s-ud' % w, 'wood', 'block/wood/planks/%s*' % w)
        add('log-placed-%s-ud' % w, 'wood', 'block/wood/bark/%s' % w)
    for w, t in [('aged', 'aged/aged*'), ('veryaged', 'aged/veryaged*')]:
        add('planks-%s-ud' % w, 'wood', 'block/wood/planks/' + t)
    for c in ['black', 'brown', 'cream', 'gray', 'orange', 'red', 'tan']:
        add('brickcourse-four-running-%s' % c, 'brick', 'block/clay/brick/four/running/%s*' % c)
    for c in ['blue', 'fire', 'black', 'brown', 'cream', 'earthyorange', 'gray', 'orange', 'red', 'tan']:
        add('tile-2x2-plain-%s' % c, 'tile', 'block/clay/tile/2x2/plain/%s*' % c)
    for t in ['plain', 'square']:
        add('plaster-%s' % t, 'plaster', 'block/stone/plaster/%s' % t)
    for c in ['ash', 'blue', 'brown', 'browngolden', 'brownlight', 'brownweathered', 'green', 'orange', 'pink', 'tan',
              'yellow']:
        add('daub-%s-normal' % c, 'plaster', 'block/clay/daub/%s/normal*' % c)
    add('water-still-7', 'water', 'block/liquid/water')
    return entries


def srgb_to_lab(rgb):
    c = np.asarray(rgb, dtype=np.float64) / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], axis=-1)


# ---- glTF reading ---------------------------------------------------------------------------------
def load_glb(path):
    data = open(path, 'rb').read()
    magic, ver, length = struct.unpack_from('<III', data, 0)
    if magic != 0x46546C67:
        raise SystemExit('not a .glb file')
    p, j, binc = 12, None, b''
    while p < length:
        clen, ctype = struct.unpack_from('<II', data, p)
        chunk = data[p + 8:p + 8 + clen]
        if ctype == 0x4E4F534A:
            j = json.loads(chunk)
        elif ctype == 0x004E4942:
            binc = chunk
        p += 8 + clen
    views = j.get('bufferViews', [])

    def view_bytes(i):
        v = views[i]
        return binc[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']], v.get('byteStride')

    def accessor(i):
        a = j['accessors'][i]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
        dt = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}[
            a['componentType']]
        raw, stride = view_bytes(a['bufferView'])
        off = a.get('byteOffset', 0)
        isz = np.dtype(dt).itemsize * n
        if stride and stride != isz:
            arr = np.lib.stride_tricks.as_strided(np.frombuffer(raw, dtype=np.uint8, offset=off),
                                                  shape=(a['count'], isz), strides=(stride, 1)).copy()
            arr = arr.view(dt).reshape(a['count'], n)
        else:
            arr = np.frombuffer(raw, dtype=dt, count=a['count'] * n, offset=off).reshape(a['count'], n)
        if a.get('normalized') and dt != np.float32:
            arr = arr.astype(np.float64) / np.iinfo(dt).max
        return arr

    images = {}

    def image(tex_index):
        if tex_index in images:
            return images[tex_index]
        src = j['textures'][tex_index]['source']
        im = j['images'][src]
        raw = view_bytes(im['bufferView'])[0] if 'bufferView' in im else open(
            os.path.join(os.path.dirname(path), im['uri']), 'rb').read()
        arr = np.asarray(Image.open(io.BytesIO(raw)).convert('RGBA'))
        images[tex_index] = arr
        return arr

    def node_matrix(n):
        if 'matrix' in n:
            return np.array(n['matrix'], dtype=np.float64).reshape(4, 4)   # column-major -> rows are columns
        t = n.get('translation', [0, 0, 0])
        r = n.get('rotation', [0, 0, 0, 1])
        s = n.get('scale', [1, 1, 1])
        x, y, z, w = r
        rm = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0],
                       [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0],
                       [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0], [0, 0, 0, 1]])
        m = np.diag([s[0], s[1], s[2], 1.0]) @ rm
        m[3, :3] = t
        return m      # row-vector form: v * m

    prims = []

    def walk(ni, parent):
        n = j['nodes'][ni]
        m = node_matrix(n) @ parent
        if 'mesh' in n:
            for pr in j['meshes'][n['mesh']]['primitives']:
                if pr.get('mode', 4) != 4:
                    continue
                pos = accessor(pr['attributes']['POSITION']).astype(np.float64)
                pos = (np.hstack([pos, np.ones((len(pos), 1))]) @ m)[:, :3]
                idx = accessor(pr['indices']).reshape(-1, 3) if 'indices' in pr else np.arange(len(pos)).reshape(-1, 3)
                mat = j['materials'][pr['material']] if 'material' in pr else {}
                pbr = mat.get('pbrMetallicRoughness', {})
                tex = img = uv = None
                if 'baseColorTexture' in pbr:
                    tex = pbr['baseColorTexture']
                    uv = accessor(pr['attributes']['TEXCOORD_%d' % tex.get('texCoord', 0)]).astype(np.float64)
                    img = image(tex['index'])
                prims.append({'pos': pos, 'idx': idx.astype(np.int64), 'uv': uv, 'img': img, 'mat': mat,
                              'factor': np.array(pbr.get('baseColorFactor', [1, 1, 1, 1]))})
        for c in n.get('children', []):
            walk(c, m)
    for ni in j['scenes'][j.get('scene', 0)]['nodes']:
        walk(ni, np.identity(4))
    return prims


def material_family(mat):
    ex = mat.get('extras', {}) or {}
    if ex.get('kind') == 'water':
        return 'water'
    if ex.get('kind') == 'effect':
        return None
    text = ' '.join([mat.get('name', '')] + [str(v) for v in (ex.get('textures') or {}).values()]).lower()
    for fam, pat in FAMILY_HINTS:
        if re.search(pat, text):
            return fam
    return 'any'


# ---- sampling and voxelisation --------------------------------------------------------------------
def sample_primitive(pr, step):
    """Points on the surface (spacing <= step) with sRGB colours (alpha-tested)."""
    P, I = pr['pos'], pr['idx']
    a, b, c = P[I[:, 0]], P[I[:, 1]], P[I[:, 2]]
    longest = np.maximum(np.maximum(np.linalg.norm(b - a, axis=1), np.linalg.norm(c - b, axis=1)),
                         np.linalg.norm(a - c, axis=1))
    ks = np.clip(np.ceil(longest / step).astype(np.int64), 1, 400)
    pts, cols = [], []
    for k in np.unique(ks):
        sel = np.nonzero(ks == k)[0]
        # barycentric grid with k subdivisions (k=1 -> centroid only)
        if k == 1:
            bc = np.array([[1 / 3, 1 / 3, 1 / 3]])
        else:
            ii, jj = np.meshgrid(np.arange(k + 1), np.arange(k + 1), indexing='ij')
            m = ii + jj <= k
            u, v = ii[m] / k, jj[m] / k
            bc = np.stack([1 - u - v, u, v], axis=1)
        for s in range(0, len(sel), max(1, 400000 // len(bc))):
            t = sel[s:s + max(1, 400000 // len(bc))]
            p = (bc[None, :, 0:1] * a[t, None] + bc[None, :, 1:2] * b[t, None] + bc[None, :, 2:3] * c[t, None])
            p = p.reshape(-1, 3)
            rgba = np.tile(pr['factor'] * 255.0, (len(p), 1))
            if pr['img'] is not None:
                U = pr['uv']
                uv = (bc[None, :, 0:1] * U[I[t, 0], None] + bc[None, :, 1:2] * U[I[t, 1], None] +
                      bc[None, :, 2:3] * U[I[t, 2], None]).reshape(-1, 2)
                h, w = pr['img'].shape[:2]
                x = np.floor((uv[:, 0] % 1.0) * w).astype(np.int64) % w
                y = np.floor((uv[:, 1] % 1.0) * h).astype(np.int64) % h
                rgba = pr['img'][y, x].astype(np.float64) * pr['factor']
            keep = rgba[:, 3] >= 128 if pr['mat'].get('alphaMode') == 'MASK' else np.ones(len(p), bool)
            pts.append(p[keep])
            cols.append(rgba[keep, :3])
    if not pts:
        return np.zeros((0, 3)), np.zeros((0, 3))
    return np.concatenate(pts), np.concatenate(cols)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('glb')
    ap.add_argument('-o', '--output', help='output base name (default: next to the .glb)')
    ap.add_argument('--scale', type=float, default=1.0, help='blocks per metre (default 1)')
    ap.add_argument('--thicken', type=int, default=0, help='grow surfaces by N blocks (fills thin gaps)')
    ap.add_argument('--no-water', action='store_true', help='leave water surfaces out')
    ap.add_argument('--families', help='only these palette families, e.g. stone,wood')
    ap.add_argument('--tile', type=int, default=1024, help='max schematic size per axis (game limit 1024)')
    ap.add_argument('--assets', default=VS_ASSETS, help='Vintage Story assets folder (for the palette)')
    ap.add_argument('--rebuild-palette', action='store_true')
    ap.add_argument('--cut', type=float, default=None, help='preview: floor plan of the blocks below this height in '
                    'blocks above the model bottom (default: the most populated layer + 15)')
    ap.add_argument('--brightness', type=float, default=1.15, help='colour multiplier before matching (textures '
                    'of Drakensang are darker than Vintage Story blocks)')
    args = ap.parse_args()
    base = args.output or os.path.splitext(args.glb)[0]

    if args.rebuild_palette or not os.path.exists(PALETTE):
        pal = build_palette(args.assets)
        json.dump(pal, open(PALETTE, 'w'), indent=1)
        print('palette: %d blocks from %s -> %s' % (len(pal), args.assets, PALETTE))
    pal = [e for e in json.load(open(PALETTE)) if e.get('enabled', True)]
    if args.families:
        allowed = set(args.families.split(',')) | {'water'}
        pal = [e for e in pal if e['family'] in allowed]
    pal_lab = srgb_to_lab(np.array([e['rgb'] for e in pal]))

    prims = load_glb(args.glb)
    step = 0.5 / args.scale      # two samples per block edge
    allp, allc, allf = [], [], []
    fam_names = list(FAMILY_BLOCKS)
    for pr in prims:
        fam = material_family(pr['mat'])
        if fam is None or (fam == 'water' and args.no_water):
            continue
        p, c = sample_primitive(pr, step)
        allp.append(p)
        allc.append(c)
        allf.append(np.full(len(p), fam_names.index(fam), dtype=np.int8))
    P, C, F = np.concatenate(allp), np.concatenate(allc), np.concatenate(allf)
    print('%d primitives, %d surface samples' % (len(prims), len(P)))

    vox = np.floor(P * args.scale).astype(np.int64)
    lo = vox.min(axis=0)
    vox -= lo
    size = vox.max(axis=0) + 1
    key = (vox[:, 1] * size[2] + vox[:, 2]) * size[0] + vox[:, 0]
    uk, inv = np.unique(key, return_inverse=True)
    n = len(uk)
    csum = np.zeros((n, 3))
    np.add.at(csum, inv, C)
    cnt = np.bincount(inv, minlength=n)
    col = csum / cnt[:, None]
    votes = np.zeros((n, len(fam_names)), dtype=np.int64)
    np.add.at(votes, (inv, F.astype(np.int64)), 1)
    fam = votes.argmax(axis=1)
    # water only where water clearly dominates; solids win otherwise
    wi = fam_names.index('water')
    solid_votes = votes.copy()
    solid_votes[:, wi] = 0
    fam = np.where((fam == wi) & (solid_votes.sum(axis=1) * 2 > votes[:, wi]), solid_votes.argmax(axis=1), fam)

    ys = uk // (size[2] * size[0])
    zs = (uk // size[0]) % size[2]
    xs = uk % size[0]
    if args.thicken > 0:
        occupied = {}
        for i in range(n):
            occupied[(xs[i], ys[i], zs[i])] = i
        add = []
        r = args.thicken
        offs = [(dx, dy, dz) for dx in range(-r, r + 1) for dy in range(-r, r + 1) for dz in range(-r, r + 1)
                if (dx, dy, dz) != (0, 0, 0) and abs(dx) + abs(dy) + abs(dz) <= r]
        for i in range(n):
            if fam[i] == wi:
                continue
            for dx, dy, dz in offs:
                k = (xs[i] + dx, ys[i] + dy, zs[i] + dz)
                if k not in occupied and min(k) >= 0:
                    occupied[k] = i
                    add.append((k, i))
        if add:
            xs = np.concatenate([xs, [k[0] for k, _ in add]])
            ys = np.concatenate([ys, [k[1] for k, _ in add]])
            zs = np.concatenate([zs, [k[2] for k, _ in add]])
            src = np.array([i for _, i in add])
            col = np.concatenate([col, col[src]])
            fam = np.concatenate([fam, fam[src]])
        size = np.array([xs.max() + 1, ys.max() + 1, zs.max() + 1])
    print('blocks: %d, size %d x %d x %d (x, y, z)' % (len(xs), size[0], size[1], size[2]))

    # choose blocks: nearest colour within the allowed palette families
    lab = srgb_to_lab(np.clip(col * args.brightness, 0, 255))
    choice = np.zeros(len(xs), dtype=np.int64)
    for fi, fname in enumerate(fam_names):
        sel = np.nonzero(fam == fi)[0]
        if not len(sel):
            continue
        cand = [k for k, e in enumerate(pal) if e['family'] in FAMILY_BLOCKS[fname]] or list(range(len(pal)))
        cand = np.array(cand)
        d = ((lab[sel, None, :] - pal_lab[None, cand, :]) ** 2).sum(axis=2)
        choice[sel] = cand[d.argmin(axis=1)]

    # schematics (tiles of at most --tile per axis)
    tile = max(16, min(1024, args.tile))
    files = []
    for tx in range(0, size[0], tile):
        for tz in range(0, size[2], tile):
            for ty in range(0, size[1], tile):
                sel = np.nonzero((xs >= tx) & (xs < tx + tile) & (zs >= tz) & (zs < tz + tile) &
                                 (ys >= ty) & (ys < ty + tile))[0]
                if not len(sel):
                    continue
                used = sorted(set(choice[sel].tolist()))
                ids = {c: i + 1 for i, c in enumerate(used)}
                sx, sy, sz = (min(tile, size[0] - tx), min(tile, size[1] - ty), min(tile, size[2] - tz))
                order = np.lexsort((xs[sel], zs[sel], ys[sel]))
                sel = sel[order]
                ind = ((ys[sel] - ty) << 20) | ((zs[sel] - tz) << 10) | (xs[sel] - tx)
                sch = {'GameVersion': '1.22.7', 'SizeX': int(sx), 'SizeY': int(sy), 'SizeZ': int(sz),
                       'BlockCodes': {str(ids[c]): pal[c]['code'] for c in used}, 'ItemCodes': {},
                       'Indices': [int(v) for v in ind], 'BlockIds': [ids[c] for c in choice[sel].tolist()],
                       'DecorIndices': [], 'DecorIds': [], 'BlockEntities': {}, 'Entities': [],
                       'ReplaceMode': 1, 'EntranceRotation': -1}
                single = size[0] <= tile and size[1] <= tile and size[2] <= tile
                name = base if single else '%s_x%d_y%d_z%d' % (base, tx, ty, tz)
                with open(name + '.json', 'w') as f:
                    json.dump(sch, f, separators=(',', ':'))
                files.append((name + '.json', tx, ty, tz, len(sel)))
    for f, tx, ty, tz, nb in files:
        print('wrote %s  (%d blocks%s)' % (f, nb, '' if len(files) == 1 else ', offset x+%d y+%d z+%d' % (tx, ty, tz)))
    print('origin of the model in blocks: %s (metres x scale)' % lo.tolist())

    # statistics and preview
    used = np.bincount(choice, minlength=len(pal))
    print('blocks used:')
    for k in np.argsort(-used)[:20]:
        if used[k]:
            print('  %-34s %7d' % (pal[k]['code'], used[k]))
    rgb = np.array([e['rgb'] for e in pal])[choice]

    def top_view(mask):
        top = np.zeros((size[2], size[0], 3), dtype=np.uint8)
        hgt = np.full((size[2], size[0]), -1)
        o = np.nonzero(mask)[0]
        o = o[np.argsort(ys[o])]
        top[zs[o], xs[o]] = rgb[o]
        hgt[zs[o], xs[o]] = ys[o]
        shade = np.where(hgt >= 0, 0.45 + 0.55 * hgt / max(1, size[1] - 1), 0)[..., None]
        return Image.fromarray(np.clip(top * shade, 0, 255).astype(np.uint8))
    full = top_view(np.ones(len(xs), bool))
    cut_y = args.cut if args.cut is not None else int(np.bincount(ys).argmax()) + 15
    cut = top_view(ys < cut_y)
    side = np.zeros((size[1], size[0], 3), dtype=np.uint8)
    order = np.argsort(-zs)
    side[size[1] - 1 - ys[order], xs[order]] = rgb[order]
    prev = Image.new('RGB', (full.width * 2 + 4, full.height + side.shape[0] + 4), (30, 30, 30))
    prev.paste(full, (0, 0))
    prev.paste(cut, (full.width + 4, 0))
    prev.paste(Image.fromarray(side), (0, full.height + 4))
    prev.save(base + '_preview.png')
    print('preview: %s_preview.png (top left: from above, top right: floor plan below y=%d blocks, '
          'bottom: from the south)' % (base, cut_y))
    fams = np.bincount(fam, minlength=len(fam_names))
    print('material families: ' + ', '.join('%s %d' % (fam_names[i], fams[i]) for i in range(len(fam_names)) if fams[i]))


if __name__ == '__main__':
    sys.exit(main())
