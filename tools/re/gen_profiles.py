#!/usr/bin/env python3
"""Generate src/core/profiles.inc: one GameAddrs table per supported executable.

Each profile lists its addresses/offsets (found with the other tools in this directory) and the
anchor locations whose bytes are verified at runtime. The bytes are read from the decrypted
in-memory dump of that executable, so the C tables never contain hand-typed signatures.
Prologues of detoured functions are taken from the dumps as well.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..')]
from game_paths import D1_DIR, ROT_DIR, dump_file  # noqa: E402
from img import Img  # noqa: E402

OUT = os.path.join(HERE, '..', '..', 'src', 'core', 'profiles.inc')
D1_DUMP = dump_file(D1_DIR, 'drakensang_mem.bin')
ROT_DUMP = dump_file(ROT_DIR, 'drakensanga1_mem.bin')

# prologue length (whole instructions, >= 5) of each detoured function
PROLOGUES = ('cam_settransform', 'set_compose_mode', 'entity_lodvisible', 'light_lodvisible', 'vis0', 'vis1',
             'var_setvector', 'fog_func', 'lodnode_render', 'pe_apply', 'tree_load', 'seggfx_create', 'level_load')

D1 = dict(
    name='Drakensang (2008)', exe='drakensang.exe', image_size=0x835000, dump=D1_DUMP,
    nstring_set=0x40136A, nstring_dtor=0x401000, narray_num=0x0C, narray_elems=0x10, release=0x4015DB,
    gfxserver_ptr=0xB766F0, gfxserver_level=0xA0, level_camera=0x10, level_rootcell=0x08, cell_bbox=0x54,
    cam_settransform=(0x60A890, 7), cam_transform=0x3A0, cam_ncamera=0x438, cam_viewproj_dirty=0x614,
    ncam_aov=0x0C, ncam_proj_dirty=0x1C,
    ui_mode='UI_PASSES_D1', sceneserver_ptr=0xB761C8, scene_renderpath=0xB8, rp_sections=0x110,
    section_stride=0x84, section_name=0x04, section_passes=0x70,
    pass_stride=0x2E4, pass_name=0x18, pass_enabled=0x2E1,
    locationmgr_ptr=0xB75FEC, change_location=0x83D1D2, locmgr_pending_len=0x7C,
    ngfxserver_ptr=0xB761D8, gfx_in_begin_frame=0x6D,
    entity_lodvisible=(0x6259BF, 9), light_lodvisible=(0x62A080, 9), lod_maxdist=0x3F0, lod_minsize=0x3F8,
    vis0=(0x5CC599, 9), vis1=None,
    fog_mode='FOG_VARSERVER', varserver_ptr=0xB761EC, var_setvector=(0x6C9EB1, 6), var_gethandle=0x6C9C4F,
    seggfx_create=(0x5F0DC1, 9), level_removeentity=0x60AB29, narray_ptr_dtor=0x48AA46, entity_cell=0x10,
    # Loader::EnvironmentLoader creates static objects with SegmentedGfxUtil (attach = 0); return address of that call
    env_seggfx_ret=0x5C5122,
    # Forest::ForestManager [0xb76700]::AddTree(nString *id, nString *file, nString *physics, matrix44 *m,
    #   vector4 *brightness, float nearLod, float farLod); finds or creates the forest cell (attached to the level)
    forest_mgr_ptr=0xB76700, forest_add_tree=0x6202A3,
    # Graphics::Cell: child cells nFixedArray<Ptr<Cell>> +0x0c, entity pools nArray[3] +0x8c (stride 0x14);
    # Graphics::Entity resource name nString +0x14
    cell_children=0x0C, cell_pool=0x8C, cell_ntypes=3, entity_resname=0x14,
    # LevelLoader::Load (cdecl, plain ret): the whole synchronous level load (the loading screen pumps messages)
    level_load=(0xA30311, 9),
    # FormationManager [0xb75fc8]: PlaceAt(matrix44*) moves the player party in formation to the matrix
    # position facing -z (Actions::TransferEntity inside a level), Finish() ends the move
    formation_mgr_ptr=0xB75FC8, formation_place=0x854E9D, formation_finish=0x854126,
    # nLodNode::RenderTransform(sceneServer, renderContext): thresholds nArray<float> +0x170, maxDistance +0x184
    lodnode_render=(0x9C45FF, 9), lodnode_thresholds=0x170, lodnode_maxdist=0x184, ncam_near=0x14, ncam_far=0x18,
    # PostEffects::Server [0xb76734]; ApplyParameters() pushes the parameter block (+0x70..) to the
    # shader variables, the global light and the sky
    pe_layout='PE_D1', pe_server_ptr=0xB76734, pe_apply=(0x5FB681, 9), pe_params=0x70,
    # nTreeServer::LoadTree(path, ., ., ., ., nearLod, farLod): SpeedTree LOD limits of a tree type
    tree_load=(0x66CE7E, 9),
    sigs=[  # (address, length)
        (0x6235D9, 5), (0x623629, 5), (0x6236F7, 7), (0x4251B9, 6),        # render path / UI::Server::Open
        (0x411AB0, 6), (0x411AD6, 3),                                      # Graphics::Server::GetCamera
        (0x60A897, 7),                                                     # SetTransform: viewProjDirty = 1
        (0x9912EA, 27), (0x83D1D2, 15), (0x83D564, 3),                     # LoadLocation -> ChangeLocation
        (0x40136A, 13), (0x4013B9, 3), (0x401000, 13),                     # nString::Set (ret 4), dtor
        (0x6CEF47, 4),                                                     # BeginFrame: inBeginFrame +0x6d
        (0x625AC0, 6), (0x625C31, 6), (0x62A174, 6), (0x5CC5A2, 7),        # LOD fields, VisibilityServer
        (0x6C9F50, 3), (0x6C9CCF, 3), (0xA18CC1, 14),                      # fog variable setter
        (0x652FA3, 5), (0x5F1626, 3), (0x60AB6B, 3), (0x4015DB, 16),       # spawning
        (0x9C46FB, 6), (0x9C4715, 6), (0x9EA25A, 6), (0x9EA452, 6), (0x9C49F3, 3),   # nLodNode min/max/thresholds
        (0x5FAE03, 8), (0x5FB6C0, 6), (0x5FB6DC, 6), (0x5FB8C4, 11), (0x5FB8E1, 6),  # PostEffects::Server
        (0x5FB907, 6), (0x5FB91A, 6), (0x5FB92D, 6), (0x5FB948, 6), (0x5FB993, 3), (0x5FBCEE, 4),
        (0x66D0C4, 9), (0x66D0D0, 9), (0x66D1D7, 3), (0x620374, 3), (0x62037B, 3),           # tree LOD limits
        (0x5C510E, 2), (0x5C511D, 5), (0xA32CCF, 7), (0x6202A3, 9), (0x620504, 5), (0x620541, 3),  # env / forest
        (0x6203B7, 5), (0x6201DE, 5), (0x62028F, 5),
        (0x4AB143, 3), (0x4AB19E, 7), (0x5F11D9, 3), (0x60AB46, 5),                               # cells, names
        (0xA30364, 5), (0x62EC16, 5),                                                             # LevelLoader::Load
        (0x981FFF, 5), (0x98200D, 5), (0x98201D, 5), (0x854F21, 3), (0x854E9D, 9), (0x854126, 7),   # formation
    ],
)

ROT_A1 = dict(
    name='The River of Time + Phileasson (drakensanga1.exe)', exe='drakensanga1.exe', image_size=0xD46000,
    dump=ROT_DUMP,
    nstring_set=0x404C50, nstring_dtor=0x401970, narray_num=0x08, narray_elems=0x0C, release=0x404120,
    gfxserver_ptr=0xFC3C60, gfxserver_level=0xA0, level_camera=0x10, level_rootcell=0x08, cell_bbox=0x54,
    cam_settransform=(0x4DD1F0, 7), cam_transform=0x398, cam_ncamera=0x448, cam_viewproj_dirty=0x624,
    ncam_aov=0x0C, ncam_proj_dirty=0x1C,
    ui_mode='UI_COMPOSEMODE', posteffects_ptr=0xFC3C68, set_compose_mode=(0x4A11D0, 9),
    pe_section=0x214, pe_mode=0x21C, section_passes=0x70,
    pass_stride=0x2E0, pass_name=0x18, pass_enabled=0x2DD,
    locationmgr_ptr=0xFC3D60, change_location=0x7DB570, locmgr_pending_len=0x70,
    ngfxserver_ptr=0xFC3E50, gfx_in_begin_frame=0x6D,
    entity_lodvisible=(0x4C4550, 9), light_lodvisible=(0x4D55F0, 9), lod_maxdist=0x3E8, lod_minsize=0x3F0,
    vis0=(0x50B0C0, 9), vis1=(0x50B1A0, 9),
    fog_mode='FOG_SHADERFUNC', fog_func=(0x946F80, 9), fog_near=(0x6C, 0x8C), fog_far=(0x70, 0x90),
    seggfx_create=0xAD9C80, level_removeentity=0x4A2450, narray_ptr_dtor=0x451CE0, entity_cell=0x10,
    ncam_near=0x14, ncam_far=0x18, pe_layout='PE_NONE',
    sigs=[
        (0x4A1236, 7), (0x4A124E, 7), (0x4A12EE, 7), (0x4A12FB, 6), (0x4A1525, 6),   # SetComposeMode, pass stride
        (0x7DDE62, 6),                                                               # PostEffectsServer singleton
        (0xAD9C8F, 7), (0x44B23E, 6), (0x44B264, 3),                                 # Graphics::Server, GetCamera
        (0x4DD1FA, 7), (0x4DD5CC, 6), (0x4DD5FC, 5),                                 # camera layout
        (0xB40243, 27), (0x7DB70E, 4), (0x7DB93E, 3),                                # LoadLocation -> ChangeLocation
        (0x404C50, 9), (0x401970, 13),                                               # nString::Set, dtor
        (0x9471E9, 7), (0x91D72A, 4),                                                # nGfxServer2, inBeginFrame
        (0x4C468A, 6), (0x4C47EA, 6), (0x4D572C, 6), (0x50B0C9, 7), (0x50B1A9, 7),   # LOD fields, VisibilityServer
        (0x9471AE, 8), (0x947019, 3), (0x947025, 3), (0x947070, 6), (0x94707F, 6),   # fog function
        (0x5C56DD, 5), (0xADA53E, 3), (0x4A2497, 3), (0x404120, 16), (0x451CE0, 6),  # spawning
    ],
)

PROFILES = [D1, ROT_A1]


def hexbytes(b):
    return ' '.join('%02x' % x for x in b)


def addr(v):
    return v[0] if isinstance(v, tuple) else (v or 0)


def emit(p, out):
    path = p['dump']
    if not os.path.exists(path):
        print('skip %s: no dump at %s' % (p['name'], path))
        return
    im = Img(path)
    pro = {}
    for k in PROLOGUES:
        v = p.get(k)
        if isinstance(v, tuple):
            pro[k] = hexbytes(im.bytes(v[0], v[1]))
    out.append('    {')
    out.append('        "%s", L"%s", 0x%X,' % (p['name'], p['exe'], p['image_size']))
    out.append('        0x%X, 0x%X, 0x%X, 0x%X, 0x%X,' % (p['nstring_set'], p['nstring_dtor'], p['narray_num'],
                                                       p['narray_elems'], p['release']))
    out.append('        0x%X, 0x%X, 0x%X, 0x%X, 0x%X,' % (p['gfxserver_ptr'], p['gfxserver_level'], p['level_camera'],
                                                       p['level_rootcell'], p['cell_bbox']))
    out.append('        0x%X, "%s", 0x%X, 0x%X, 0x%X, 0x%X, 0x%X,' % (
        addr(p['cam_settransform']), pro['cam_settransform'], p['cam_transform'], p['cam_ncamera'],
        p['cam_viewproj_dirty'], p['ncam_aov'], p['ncam_proj_dirty']))
    out.append('        %s,' % p['ui_mode'])
    out.append('        0x%X, 0x%X, 0x%X, 0x%X, 0x%X, 0x%X,' % tuple(
        p.get(k, 0) for k in ('sceneserver_ptr', 'scene_renderpath', 'rp_sections', 'section_stride',
                              'section_name', 'section_passes')))
    out.append('        0x%X, 0x%X, "%s", 0x%X, 0x%X,' % (
        p.get('posteffects_ptr', 0), addr(p.get('set_compose_mode')), pro.get('set_compose_mode', ''),
        p.get('pe_section', 0), p.get('pe_mode', 0)))
    out.append('        0x%X, 0x%X, 0x%X,' % (p['pass_stride'], p['pass_name'], p['pass_enabled']))
    out.append('        0x%X, 0x%X, 0x%X,' % (p['locationmgr_ptr'], p['change_location'], p['locmgr_pending_len']))
    out.append('        0x%X, 0x%X,' % (p['ngfxserver_ptr'], p['gfx_in_begin_frame']))
    out.append('        0x%X, 0x%X, "%s", "%s", 0x%X, 0x%X,' % (
        addr(p['entity_lodvisible']), addr(p['light_lodvisible']), pro['entity_lodvisible'],
        pro['light_lodvisible'], p['lod_maxdist'], p['lod_minsize']))
    out.append('        {0x%X, 0x%X}, {"%s", "%s"},' % (addr(p.get('vis0')), addr(p.get('vis1')),
                                                    pro.get('vis0', ''), pro.get('vis1', '')))
    out.append('        %s, 0x%X, 0x%X, 0x%X, "%s",' % (
        p['fog_mode'], p.get('varserver_ptr', 0), addr(p.get('var_setvector')), p.get('var_gethandle', 0),
        pro.get('var_setvector', '')))
    fn, ff = p.get('fog_near', (0, 0)), p.get('fog_far', (0, 0))
    out.append('        0x%X, "%s", {0x%X, 0x%X}, {0x%X, 0x%X},' % (addr(p.get('fog_func')), pro.get('fog_func', ''),
                                                                 fn[0], fn[1], ff[0], ff[1]))
    out.append('        0x%X, 0x%X, 0x%X, 0x%X,' % (addr(p['seggfx_create']), p['level_removeentity'],
                                                 p['narray_ptr_dtor'], p['entity_cell']))
    out.append('        0x%X, "%s", 0x%X, 0x%X, 0x%X, 0x%X,' % (
        addr(p.get('lodnode_render')), pro.get('lodnode_render', ''), p.get('lodnode_thresholds', 0),
        p.get('lodnode_maxdist', 0), p['ncam_near'], p['ncam_far']))
    out.append('        %s, 0x%X, 0x%X, "%s", 0x%X,' % (p['pe_layout'], p.get('pe_server_ptr', 0), addr(p.get('pe_apply')),
                                                   pro.get('pe_apply', ''), p.get('pe_params', 0)))
    out.append('        0x%X, "%s",' % (addr(p.get('tree_load')), pro.get('tree_load', '')))
    out.append('        "%s", 0x%X, 0x%X, 0x%X,' % (pro.get('seggfx_create', ''), p.get('env_seggfx_ret', 0),
                                                p.get('forest_mgr_ptr', 0), p.get('forest_add_tree', 0)))
    out.append('        0x%X, 0x%X, %d, 0x%X,' % (p.get('cell_children', 0), p.get('cell_pool', 0), p.get('cell_ntypes', 0),
                                              p.get('entity_resname', 0)))
    out.append('        0x%X, "%s",' % (addr(p.get('level_load')), pro.get('level_load', '')))
    out.append('        0x%X, 0x%X, 0x%X,' % (p.get('formation_mgr_ptr', 0), p.get('formation_place', 0),
                                          p.get('formation_finish', 0)))
    out.append('        {')
    for a, n in p['sigs']:
        out.append('            {0x%08X, "%s"},' % (a, hexbytes(im.bytes(a, n))))
    out.append('        },')
    out.append('    },')
    print('%-55s %d signatures' % (p['name'], len(p['sigs'])))


def main():
    out = ['/* Generated by tools/re/gen_profiles.py from the decrypted dumps - do not edit. */']
    for p in PROFILES:
        emit(p, out)
    with open(OUT, 'w', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', os.path.normpath(OUT))


if __name__ == '__main__':
    sys.exit(main())
