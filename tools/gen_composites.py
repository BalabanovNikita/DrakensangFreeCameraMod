#!/usr/bin/env python3
"""Generate drakensang_mod_composites.txt: "composite" levels the mod assembles at runtime.

Ferdok (location02) is split into four district levels that share one coordinate system; each
contains its own district in detail, low-poly stand-ins for the others and barrier walls on the
district borders. The composite loads Praios Square, hides its barriers and stand-ins while the
level loads, and places the detailed buildings, props and SpeedTrees of the other three districts.
A forest of SpeedTrees is planted on the terrain outside the city walls.

File format (UTF-8):
  [composite]  id= title= base= entry= hide=<resource substring> (repeatable)
  m|<gfx resource>|<16 floats>                          static model
  t|<tree file>|<16 floats>|near far|b0 b1 b2 b3        SpeedTree
  [end]

Usage: gen_composites.py [--out DIR]   (default: the game folder, see game_paths.py)
"""
import argparse
import math
import os
import random
import re
import sqlite3
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_paths import need_game  # noqa: E402
from gen_extras import toc, PACKAGES, D1_DIR  # noqa: E402

OUT_NAME = 'drakensang_mod_composites.txt'
BASE = 'location02_praios'
OTHERS = ['location02_hafen', 'location02_grafenstadt', 'location02_fuhrmannsheim']
VISUAL_TABLES = ['_Instance__Environment', '_Instance_DestroyableEntity', '_Instance_Door', '_Instance_Chest',
                 '_Instance_StateObject', '_Instance_WorkBench', '_Instance_Object', '_Instance_BookStand']
# not taken from the other districts: their terrain and wall variants, stand-ins for neighbouring
# districts, district barriers, things every district already has, story leftovers, helpers
DROP = ['aussen_boden', 'ferdok_mauern_', 'ferdok_stadtmauer_', 'ferdok_randhaeuser_', '_haeuser_low',
        'sperrmauer', 'sperr_mauer', 'portalplane_sperre', 'blockade_fuhrwerk', 'skies/', 'mattes/',
        'location02_wasser', 'ferdock_felsen', 'ferdok_felswand', 'blut_pfuetze', 'dummies/', 'loc02_xo']
# hidden in the base level while it loads: barriers to the other districts and their stand-ins
HIDE = ['praios_sperrmauer', '/sperr_mauer', 'praios_haeuser_low', 'portalplane_sperre']

# forest outside the walls (Avestrue species): (file, weight, nearLod, farLod, scale range)
TREES = [('eiche/eiche_aussen_01', 22, 10, 80, (0.9, 1.15)), ('eiche/eiche_aussen_02', 18, 10, 80, (0.9, 1.15)),
         ('eiche/eiche_aussen_mitte_02', 22, 10, 70, (0.85, 1.1)), ('eiche/eiche_innen_01', 14, 10, 80, (0.85, 1.15)),
         ('eiche/fichte_06', 12, 10, 50, (0.9, 1.2)), ('eiche/eiche_small_02', 12, 10, 60, (0.8, 1.15))]
BUSHES = [('gebuesch/gebuesch_05', 45, 10, 50, (0.7, 1.15)), ('gebuesch/gebuesch_07', 45, 10, 50, (0.9, 1.3)),
          ('gebuesch/kr_gebuesch_01', 10, 10, 50, (1.0, 1.3))]
WATER_Y = -8.3


def load_files():
    files = {}
    for p in PACKAGES:
        for k, v in toc(os.path.join(D1_DIR, p)).items():
            files.setdefault(k, v)
    return files


FILES = None
_fh = {}


def rd(name):
    path, off, size = FILES[name]
    f = _fh.setdefault(path, open(path, 'rb'))
    f.seek(off)
    return f.read(size)


def mesh(name):
    """(vertices [(x,y,z)], triangles [(i,j,k)]) of an nvx2 mesh."""
    d = rd('export/meshes/' + name)
    _, ng, nv, w, nt, ne, mask = struct.unpack_from('<4s6i', d, 0)
    base = 28 + ng * 24
    verts = [struct.unpack_from('<3f', d, base + i * w * 4) for i in range(nv)]
    ib = base + nv * w * 4
    tris = [struct.unpack_from('<3H', d, ib + i * 6) for i in range(nt)]
    return verts, tris


def world(m, v):
    x, y, z = v
    return (x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13],
            x * m[2] + y * m[6] + z * m[10] + m[14])


def scene_meshes(res):
    sc = rd('export/gfxlib/%s.n2' % res.lower())
    ms = sorted(set(x.decode().lower() for x in re.findall(rb'meshes:([\w/.\-]+?\.nvx2)', sc)))
    return [x for x in ms if not re.search(r'_c_\d+\.nvx2$', x)]


class Ground:
    """Height of a triangle mesh in world space, by xz lookup in a grid."""
    CELL = 16.0

    def __init__(self, tris):
        self.tris, self.grid = tris, {}
        for t in tris:
            xs, zs = [p[0] for p in t], [p[2] for p in t]
            for gx in range(int(math.floor(min(xs) / self.CELL)), int(math.floor(max(xs) / self.CELL)) + 1):
                for gz in range(int(math.floor(min(zs) / self.CELL)), int(math.floor(max(zs) / self.CELL)) + 1):
                    self.grid.setdefault((gx, gz), []).append(t)

    def at(self, x, z):
        """(height, normal y) or None."""
        best = None
        for a, b, c in self.grid.get((int(math.floor(x / self.CELL)), int(math.floor(z / self.CELL))), []):
            d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(d) < 1e-9:
                continue
            l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
            l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
            l3 = 1 - l1 - l2
            if min(l1, l2, l3) < -1e-4:
                continue
            h = l1 * a[1] + l2 * b[1] + l3 * c[1]
            u = [b[i] - a[i] for i in range(3)]
            v = [c[i] - a[i] for i in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            ln = math.sqrt(sum(q * q for q in n)) or 1
            cand = (h, abs(n[1]) / ln)
            if best is None or cand[0] > best[0]:
                best = cand
        return best


def hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]


def dist_to_poly(p, poly):
    """Signed distance to a convex polygon (negative inside)."""
    inside, best = True, 1e30
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        ex, ez = b[0] - a[0], b[1] - a[1]
        if ex * (p[1] - a[1]) - ez * (p[0] - a[0]) < 0:
            inside = False
        t = max(0.0, min(1.0, ((p[0] - a[0]) * ex + (p[1] - a[1]) * ez) / (ex * ex + ez * ez)))
        dx, dz = p[0] - (a[0] + t * ex), p[1] - (a[1] + t * ez)
        best = min(best, math.sqrt(dx * dx + dz * dz))
    return -best if inside else best


def noise(x, z, seed=7):
    """Smooth value noise in [0, 1]."""
    def h(i, j):
        r = random.Random((i * 73856093) ^ (j * 19349663) ^ seed)
        return r.random()
    i, j = math.floor(x), math.floor(z)
    fx, fz = x - i, z - j
    sx, sz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    a = h(i, j) + (h(i + 1, j) - h(i, j)) * sx
    b = h(i, j + 1) + (h(i + 1, j + 1) - h(i, j + 1)) * sx
    return a + (b - a) * sz


def pick(rng, table):
    total = sum(t[1] for t in table)
    r = rng.uniform(0, total)
    for t in table:
        r -= t[1]
        if r <= 0:
            return t
    return table[-1]


def tree_matrix(rng, x, y, z, scale):
    a = rng.uniform(0, 2 * math.pi)
    c, s = math.cos(a) * scale, math.sin(a) * scale
    return [c, 0, -s, 0, 0, scale, 0, 0, s, 0, c, 0, x, y, z, 1]


def fmt(m):
    return ' '.join('%.4f' % v for v in m)


def main():
    ap = argparse.ArgumentParser(description='Generate the composite levels.')
    ap.add_argument('--out', help='output directory (default: the game folder)')
    args = ap.parse_args()
    need_game(D1_DIR)
    out = os.path.join(args.out or D1_DIR, OUT_NAME)
    global FILES
    FILES = load_files()
    db = sqlite3.connect('file:%s/export/db/game.db4?mode=ro' % D1_DIR, uri=True)
    db.text_factory = lambda b: b.decode('cp1252', 'replace')

    def key(res, m):
        return (res.lower(), round(m[12] * 4), round(m[13] * 4), round(m[14] * 4))

    seen, occupied, models, trees = set(), [], [], []
    for lvl in [BASE] + OTHERS:
        for t in VISUAL_TABLES:
            for res, tr in db.execute('select Graphics, Transform from "%s" where _Level=? and Graphics<>\'\'' % t,
                                      (lvl,)):
                m = struct.unpack('<16f', tr)
                occupied.append((m[12], m[14]))
                k = key(res, m)
                if k in seen:
                    continue
                seen.add(k)
                if lvl != BASE and not any(d in res.lower() for d in DROP):
                    models.append((res, m))
        for f, tr, n, fa, b0, b1, b2, b3 in db.execute(
                'select File, Transform, NearLod, FarLod, BranchBrightness, FrondBrightness, LeafBrightness, '
                'BillboardBrightness from _Instance_Tree where _Level=?', (lvl,)):
            m = struct.unpack('<16f', tr)
            occupied.append((m[12], m[14]))
            k = key('tree:' + f, m)
            if k in seen:
                continue
            seen.add(k)
            if lvl != BASE:
                trees.append((f, m, n or 10, fa or 35, (b0, b1, b2, b3)))
    print('from the other districts: %d models, %d SpeedTrees' % (len(models), len(trees)))

    # ---- forest outside the walls --------------------------------------------------------------
    rows = {r: struct.unpack('<16f', tr) for r, tr in db.execute(
        'select Graphics, Transform from _Instance__Environment where _Level=?', (BASE,))}
    gm = rows['_location_02_grounds/aussen_boden_praios']
    tris = []
    for mname in scene_meshes('_location_02_grounds/aussen_boden_praios'):
        v, t = mesh(mname)
        w = [world(gm, p) for p in v]
        tris += [(w[a], w[b], w[c]) for a, b, c in t]
    ground = Ground(tris)
    wm = rows['_location_02_elements/ferdok_stadtmauer_praios']
    wall = []
    for mname in scene_meshes('_location_02_elements/ferdok_stadtmauer_praios'):
        v, _ = mesh(mname)
        wall += [(round(q[0], 1), round(q[2], 1)) for q in (world(wm, p) for p in v)]
    city = hull(wall)
    water = []
    for res in ['_location_02_grounds/location02_wasser', '_location_02_grounds/aussen_boden_wasser']:
        m = rows.get(res)
        if not m:
            continue
        for mname in scene_meshes(res):
            v, t = mesh(mname)
            w = [world(m, p) for p in v]
            water += [(w[a], w[b], w[c]) for a, b, c in t]
    water_g = Ground(water)
    occ_grid = {}
    for x, z in occupied:
        occ_grid.setdefault((int(x // 20), int(z // 20)), []).append((x, z))

    def near_object(x, z, r):
        for gx in range(int(x // 20) - 1, int(x // 20) + 2):
            for gz in range(int(z // 20) - 1, int(z // 20) + 2):
                for ox, oz in occ_grid.get((gx, gz), []):
                    if (ox - x) ** 2 + (oz - z) ** 2 < r * r:
                        return True
        return False

    def near_water(x, z, r=6):
        return any(water_g.at(x + dx, z + dz) for dx, dz in ((0, 0), (r, 0), (-r, 0), (0, r), (0, -r)))

    rng = random.Random(1234)
    xs = [t[0][0] for t in tris] + [t[1][0] for t in tris]
    zs = [t[0][2] for t in tris] + [t[1][2] for t in tris]
    x0, x1, z0, z1 = min(xs) + 30, max(xs) - 30, min(zs) + 30, max(zs) - 30
    placed, grid = [], {}

    def free(x, z, r):
        for gx in range(int(x // 10) - 2, int(x // 10) + 3):
            for gz in range(int(z // 10) - 2, int(z // 10) + 3):
                for px, pz, pr in grid.get((gx, gz), []):
                    if (px - x) ** 2 + (pz - z) ** 2 < (max(r, pr)) ** 2:
                        return False
        return True

    forest = []
    for _ in range(200000):
        x, z = rng.uniform(x0, x1), rng.uniform(z0, z1)
        d = dist_to_poly((x, z), city)
        if d < 22 or d > 380:
            continue
        dens = noise(x / 110, z / 110) * 0.75 + noise(x / 35, z / 35, 11) * 0.25
        edge = min(1.0, (d - 22) / 40)                       # thinner right outside the walls
        bush = dens < 0.56
        if dens < 0.50 or rng.random() > edge:
            continue
        r = 7.0 if bush else 8.5
        if not free(x, z, r) or near_object(x, z, 14):
            continue
        g = ground.at(x, z)
        if not g or g[1] < 0.88 or g[0] < WATER_Y + 1.0 or near_water(x, z):
            continue
        spec = pick(rng, BUSHES if bush else TREES)
        s = rng.uniform(*spec[4])
        forest.append((spec[0], tree_matrix(rng, x, g[0] - 0.3, z, s), spec[2], spec[3], (1.0, 1.0, 1.0, 1.0)))
        grid.setdefault((int(x // 10), int(z // 10)), []).append((x, z, r))
        placed.append((x, z))
    nb = sum(1 for f in forest if f[0].startswith('gebuesch'))
    print('forest outside the walls: %d trees, %d bushes' % (len(forest) - nb, nb))

    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Drakensang mod: composite levels (generated by _mod/tools/gen_composites.py)\n')
        f.write('[composite]\nid=location02_full\n')
        f.write('title=Ferdok — whole city (all districts, forest outside the walls)\n')
        f.write('base=%s\nentry=Loc02_Entry_Start\n' % BASE)
        for h in HIDE:
            f.write('hide=%s\n' % h)
        for res, m in models:
            f.write('m|%s|%s\n' % (res, fmt(m)))
        for fl, m, n, fa, b in trees + forest:
            f.write('t|%s|%s|%g %g|%g %g %g %g\n' % (fl, fmt(m), n, fa, b[0], b[1], b[2], b[3]))
        f.write('[end]\n')
    print('wrote %s (%d models, %d trees)' % (out, len(models), len(trees) + len(forest)))


if __name__ == '__main__':
    sys.exit(main())
