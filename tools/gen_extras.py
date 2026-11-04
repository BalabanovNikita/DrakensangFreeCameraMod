#!/usr/bin/env python3
"""Generate the extras lists: unused / cut locations that the mod can place in the running game
(F7 -> star entry) and view with the free camera.

Usage: gen_extras.py [d1|rot|rot_a1|all] [--out DIR]
'all' (default) covers the games found through game_paths.py. Without --out the file goes to the
game folder.

Each line:  title|gfx resource|cx cy cz yaw pitch|minx miny minz maxx maxy maxz
The camera values are relative to the model origin (yaw/pitch in degrees, free-camera convention:
forward = (sin yaw * cos pitch, sin pitch, cos yaw * cos pitch)).
The bounding box comes from the visual meshes the .n2 scene references.
"""
import argparse
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_paths import D1_DIR, ROT_DIR  # noqa: E402

PACKAGES = ['patch1.npk', 'export.npk', 'export_1.npk', 'export_2.npk', 'export_3.npk']   # patch first

# (title, resource, camera) - camera None = automatic overview from outside
D1_ENTRIES = [
    # cut locations
    ('Murolosh: Temple of Angrosch (cut)', '_location_09_grounds/dungeon_murolosch_tempel', (4, 4, 0, -90, -3)),
    ('Murolosh: temple side hall (cut)', '_location_09_grounds/dungeon_murolosch_h_raum_tempel', None),
    ('Tallon: Haunted House (cut quest)', '_location_08_grounds/dungeon_spukhaus', None),
    ('Tallon: Haunted House, final room (cut)', '_location_08_grounds/dungeon_spukhaus_endraum', None),
    ('Murolosh: full prison (game uses a reduced one)', '_location_09_grounds/dungeon_murolosch_gefaengnis', None),
    ('Murolosh lift level: staircase A (cut)', '_location_09_grounds/dungeon_murolosch_h_treppenhaus', None),
    ('Murolosh lift level: staircase B (cut)', '_location_09_grounds/dungeon_murolosch_h_treppenhaus_01', None),
    ('Murolosh lift level: lift room (cut)', '_location_09_grounds/dungeon_murolosch_l_raum_fahrstuhl_03', None),
    ('Murolosh lift level: lower lift station (cut)', '_location_09_grounds/dungeon_murolosch_h_aufzug_unten', None),
    ('Murolosh lift level: lift hall (cut)', '_location_09_grounds/dungeon_murolosch_h_raum_aufzug', None),
    ('Murolosh: teleport room (unused)', '_location_09_grounds/dungeon_murolosch_l_raum_teleport_neu', None),
    ('Murolosh: gate room (unused)', '_location_09_grounds/dungeon_murolosch_l_raum_tor', None),
    ('Tallon dwarf tower dungeon: room (cut, kit piece)', '_location_08_grounds/dungeon_zwergenturm_l_raum', None),
    ('Tallon dwarf tower dungeon: lift room (cut, kit piece)', '_location_08_grounds/dungeon_zwergenturm_m_raum_fahrstuhl', None),
    # old versions of existing locations
    ('Ferdok: Town House library, old version', '_location_02_grounds/dungeon_stadthaus_bibliothek', None),
    ('Ferdok: Temple of Hesinde interior, first version', '_location_02_grounds/dungeon_ferdok_s_hesindetempel_innen', None),
    ('Ferdok: Silver Pitcher on Praios Square, old version', '_location_02_grounds/dungeon_praiosplatz_silberkrug', None),
    ('Ferdok: cellar kitchen (unused)', '_location_02_grounds/dungeon_keller_s_raum_kueche', None),
    ('Ferdok sewers: cultists\' room (unused)', '_location_02_grounds/dungeon_kanal_s_raum_kultisten', None),
    ('Ferdok: warehouse, old version', '_location_02_grounds/dungeon_lagerhaus_01', None),
    ('Blood Mountains: manor annex (unused)', '_location_03_grounds/gutshaus_add', None),
    ('Tallon: crypt entrance room (unused)', '_location_08_grounds/dungeon_gruft_s_raum_eingang', None),
    ('Dragon\'s Lair: exterior terrain, unused version', '_location_08_grounds/loc08_aussen_boden_drachenhort', None),
    ('Old Mineshaft: goblin room (unused)', '_location_01_grounds/dungeon_baerenhoehle_m_raum_goblins', None),
    ('Old Mineshaft: Rakorium\'s room (unused)', '_location_01_grounds/dungeon_baerenhoehle_m_rakorium', None),
    ('Blood Mountains: alternative terrain (unused)', '_location_03_grounds/aussen_boden_vs', None),
    # early terrain drafts
    ('Avestrue: early terrain draft 1', '_location_01_grounds/aussen_boden_layout', None),
    ('Avestrue: early terrain draft 2', '_location_01_grounds/aussen_boden_layout_02', None),
    ('Ferdok: early terrain draft 1', '_location_02_grounds/aussen_boden_layout', None),
    ('Ferdok: early terrain draft 2', '_location_02_grounds/aussen_boden_layout_02', None),
    ('Ferdok: early ground', '_location_02_grounds/aussen_boden', None),
    ('Above the Silver Falls: early terrain draft', '_location_05_grounds/aussen_boden_layout', None),
    ('Tallon: early terrain draft', '_location_08_grounds/aussen_boden_layout', None),
    ('Deeps of Gruldur: early terrain draft', '_location_10_grounds/aussen_boden_layout', None),
    # developer levels
    ('Developer combat arena (test level)', '_location_00_grounds/aussen_boden_kampfkammer', None),
    ('Developer test chamber', '_location_00_grounds/aussen_boden_spkammer', None),
    ('Random encounter terrain, unused variant 1', '_location_00_grounds/aussen_boden_zufall_01', None),
    ('Random encounter terrain, unused variant 2', '_location_00_grounds/aussen_boden_zufall_02', None),
]

# The River of Time: level graphics in gfxlib/locr*/ that no world database (game, gamea1, addon1,
# patch1, static, statica1) references. Entries from addon1.npk only exist in drakensanga1.exe.
ROT_ENTRIES = [
    # cut locations and rooms
    ('Thûrstein Tollgate: sewers (cut)', 'locr02/locr02_kanalisation', None),
    ('Nadoret: werewolf house interior (cut)', 'locr01/locr01_innen_wehrwolf_01', None),
    ('Hammerberg: pirate tavern, unused version', 'locr04/locr04_piratenlager_taverne', None),
    ('Dwarven Delvings: stair tower with mine carts (unused)', 'locr04/locr04_d_zb_h_raum_treppenturm_loren_01', None),
    ('Dwarven Delvings: quarters 1 (unused)', 'locr04/locr04_d_zb_h_raum_unterkunft_01', None),
    ('Dwarven Delvings: quarters 2 (unused)', 'locr04/locr04_d_zb_h_raum_unterkunft_02', None),
    ('Ancient Temple of Efferd: root shaft, first version', 'locr06/locr06_d_fv_h_raum_schacht_hauptwurzel_01', None),
    ('Ancient Temple of Efferd: room 2 (unused)', 'locr06/locr06_d_fv_raum_02', None),
    ('Ancient Temple of Efferd: room 3 (unused)', 'locr06/locr06_d_fv_raum_03', None),
    ('Ancient Temple of Efferd: room 10 (unused)', 'locr06/locr06_d_fv_raum_10', None),
    ('Thûrstein Tollgate dungeon: room 1, early version', 'locr02/locr02_d_km_raum01', None),
    ('Thûrstein Tollgate dungeon: room 5, mirrored (unused)', 'locr02/locr02_d_km_raum_05_mirror', None),
    ('Treasure Cave: side room (cut)', 'locr02/a1_d_geist1_seitenraum_01', None),
    # early versions and placeholders
    ('Gorge: early corridor draft', 'locr05/temp_schlucht_gang_01', None),
    ('Gorge: cave module draft', 'locr05/temp_locr05_hoehle_modul_01', None),
    ('Nadoret Castle: placeholder model', 'locr05/temp_dummy_schloss', None),
    ('Nadoret Castle: complete castle model (unused)', 'locr05/locr05_schloss', None),
    ('Nadoret Castle: treasury, early version', 'locr05/temp_locr05_schloss_hof_schatzkammer', None),
    ("Castle Cellar: jailer's room, early version", 'locr05/temp_locr05_schloss_kerker_kerkermeister', None),
    ('Castle Cellar: room, early version', 'locr05/temp_locr05_schloss_kerker_raum_01', None),
    ('Tollgate: early fortress layout', 'locr02/temp_zollfestung', None),
    ('Nadoret: town stairs terrain (unused)', 'locr01/locr01_aussen_boden_stadt_treppen_01', None),
    ("Water Dragon's Lair: grass terrain variant (unused)", 'locr08/locr08_aussen_ground_gras', None),
    # Phileasson add-on: Tie'Shianna variants that are never used
    ("Tie'Shianna: entrance hall at night", 'a1_locr10/a1_locr10_eingangshalle_nacht', None),
    ("Tie'Shianna: entrance hall, destroyed", 'a1_locr10/a1_locr10_eingangshalle_dest', None),
    ("Tie'Shianna: throne room at night", 'a1_locr10/a1_locr10_thronsaal_nacht', None),
    ("Tie'Shianna: throne room final battle at night", 'a1_locr10/a1_locr10_thronsaal_endkampf_nacht', None),
    ("Tie'Shianna: library at night", 'a1_locr10/a1_locr10_bibliothek_nacht', None),
    ("Tie'Shianna: atrium at night", 'a1_locr10/a1_locr10_atrium_nacht', None),
    ("Tie'Shianna: Lion Gate at night", 'a1_locr10/a1_locr10_loewentor_nacht', None),
    ("Tie'Shianna: corridor at night", 'a1_locr10/a1_locr10_gang_01_nacht', None),
    ("Tie'Shianna: withered corridor at night", 'a1_locr10/a1_locr10_gang_verwelkt_nacht', None),
]

# name: (game dir, packages (first wins), entries, output file)
PRESETS = {
    'd1': (D1_DIR, PACKAGES, D1_ENTRIES, 'drakensang_mod_extras.txt'),
    'rot': (ROT_DIR, PACKAGES, ROT_ENTRIES, 'drakensang_mod_extras_drakensang.txt'),
    'rot_a1': (ROT_DIR, ['patch1.npk', 'addon1.npk'] + PACKAGES[1:], ROT_ENTRIES,
               'drakensang_mod_extras_drakensanga1.txt'),
}


def toc(path):
    res = {}
    with open(path, 'rb') as f:
        _, _, dataoff = struct.unpack('<4sii', f.read(12))
        stack = []
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            fcc, ln = struct.unpack('<4si', hdr)
            body = f.read(ln)
            if fcc == b'_RID':
                n = struct.unpack('<h', body[:2])[0]
                stack.append(body[2:2 + n].decode('latin1'))
            elif fcc == b'ELIF':
                off, size, n = struct.unpack('<iih', body[:10])
                name = '/'.join(stack + [body[10:10 + n].decode('latin1')]).lower()
                res.setdefault(name, (path, off + dataoff + 8, size))
            elif fcc == b'DNED':
                stack.pop()
            elif fcc == b'ATAD':
                break
    return res


def generate(game, packages, entries, outname, outdir=None):
    out = os.path.join(outdir or game, outname)
    files = {}
    for p in packages:
        for k, v in toc(os.path.join(game, p)).items():
            files.setdefault(k, v)

    def read(name):
        path, off, size = files[name]
        with open(path, 'rb') as f:
            f.seek(off)
            return f.read(size)

    lines = []
    for title, res, cam in entries:
        key = 'export/gfxlib/%s.n2' % res.lower()
        if key not in files:
            print('skip (no scene):', res)
            continue
        scene = read(key)
        meshes = sorted(set(m.decode().lower() for m in re.findall(rb'meshes:([\w/.\-]+?\.nvx2)', scene)))
        visual = [m for m in meshes if not re.search(r'_c_\d+\.nvx2$', m)]
        mn, mx, nverts = [1e30] * 3, [-1e30] * 3, 0
        for m in visual:
            k = 'export/meshes/' + m
            if k not in files:
                continue
            d = read(k)
            _, ngroups, nv, width, _, _, mask = struct.unpack_from('<4s6i', d, 0)
            if not mask & 1:
                continue
            base = 28 + ngroups * 24
            for i in range(nv):
                x, y, z = struct.unpack_from('<3f', d, base + i * width * 4)
                mn = [min(a, b) for a, b in zip(mn, (x, y, z))]
                mx = [max(a, b) for a, b in zip(mx, (x, y, z))]
            nverts += nv
        if nverts < 50:
            print('skip (no visual geometry):', res, meshes)
            continue
        if cam is None:
            c = [(a + b) / 2 for a, b in zip(mn, mx)]
            ext = [(b - a) / 2 for a, b in zip(mn, mx)]
            r = max(ext[0], ext[2], 4.0)
            pos = (c[0] - 0.9 * ext[0] - 2, mx[1] + 0.35 * r + 3, c[2] - 0.9 * ext[2] - 2)
            d = [c[0] - pos[0], c[1] - pos[1], c[2] - pos[2]]
            ln = math.sqrt(sum(v * v for v in d))
            yaw = math.degrees(math.atan2(d[0], d[2]))
            pitch = math.degrees(math.asin(d[1] / ln))
            cam = (pos[0], pos[1], pos[2], yaw, pitch)
        lines.append('%s|%s|%.2f %.2f %.2f %.1f %.1f|%.1f %.1f %.1f %.1f %.1f %.1f'
                     % ((title, res) + tuple(cam) + tuple(mn) + tuple(mx)))
        print('%-55s %6d verts  size %.0fx%.0fx%.0f' % (title, nverts, mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]))

    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Drakensang mod: unused / cut locations (title|gfx resource|camera x y z yaw pitch|bbox min max)\n')
        f.write('# Generated by _mod/tools/gen_extras.py\n')
        f.write('\n'.join(lines) + '\n')
    print('wrote %d entries to %s' % (len(lines), out))


def main():
    ap = argparse.ArgumentParser(description='Generate the lists of unused and cut locations.')
    ap.add_argument('which', nargs='?', default='all', choices=list(PRESETS) + ['all'])
    ap.add_argument('--out', help='output directory (default: the game folder)')
    args = ap.parse_args()
    done = 0
    for name, (game, packages, entries, outname) in PRESETS.items():
        if args.which in (name, 'all') and game and os.path.exists(os.path.join(game, packages[-1])):
            print('==', name)
            generate(game, packages, entries, outname, args.out)
            done += 1
    if not done:
        sys.exit('no game found: set DRAKENSANG_DIR and/or DRAKENSANG_ROT_DIR')


if __name__ == '__main__':
    sys.exit(main())
