/* Place graphics models from the game's files (gfxlib:<name>.n2) into the current level, e.g. the
 * unused Temple of Angrosch, using the game's own SegmentedGfxUtil. Graphics only: no physics,
 * no game entity. Spawned models are removed on demand, on level change and on core unload.
 *
 * Requests are queued and created from the message pump outside the render frame, a batch per
 * pump call, so composite levels with hundreds of objects load without a long stall. SpeedTrees
 * are added to the level's forest through Forest::ForestManager::AddTree (they live as long as
 * the level). Objects of the level whose resource matches a hide pattern can be detached from the
 * level's cells after it has loaded (they stay owned by the game; it removes them as usual).
 * Nothing is ever created or changed while a level loads: freeing objects inside the environment
 * loader corrupted the heap. */
#include "game.h"

#define MAX_SPAWNED 16384
#define MAX_QUEUE   8192
#define PER_PUMP    24
#define MAX_HIDE    32

static BYTE *g_ents[MAX_SPAWNED];
static int g_nents, g_ntrees, g_treeSerial;
static BYTE *g_level;          /* level the models were attached to */
static int g_ok = -1, g_treeOk = -1;

typedef struct SpawnReq {
    char  res[96];
    float m[16];
    float nearLod, farLod;     /* trees */
    float bright[4];
    int   tree;
} SpawnReq;
static SpawnReq *g_queue;      /* HeapAlloc'ed ring */
static int g_qhead, g_qcount, g_pumpQueued;
static BYTE *g_qlevel;         /* level the queued requests are meant for */

static char g_hide[MAX_HIDE][48];
static int g_nhide, g_hidden;

typedef void *(__fastcall *PFN_SegCreate)(void *util, void *edx, void *ret, const void *res, const float *m, int attach);
typedef void (__fastcall *PFN_ThisVoid)(void *self, void *edx);
typedef void (__fastcall *PFN_ThisPtr)(void *self, void *edx, void *arg);
typedef void (__fastcall *PFN_AddTree)(void *mgr, void *edx, void *id, void *file, void *physics, const float *m,
                                       const float *bright, float nearLod, float farLod);

static int name_hidden(const char *res)
{
    int i;
    for (i = 0; i < g_nhide; i++)
        if (strstr(res, g_hide[i])) return 1;
    return 0;
}

static int available(void)
{
    if (g_ok < 0) {
        g_ok = game_ok();
        g_treeOk = g_ok && G->forest_mgr_ptr && G->forest_add_tree;
        mlog("spawn: %s (trees %d, hide %d)", g_ok ? "available" : "DISABLED", g_treeOk, G && G->cell_pool != 0);
    }
    return g_ok;
}

/* Hide patterns for the next detach pass (NULL / 0 = none). */
void spawn_set_hide(const char (*patterns)[48], int n)
{
    int i;
    available();
    g_nhide = 0;
    g_hidden = 0;
    for (i = 0; i < n && i < MAX_HIDE; i++) strcpy(g_hide[g_nhide++], patterns[i]);
}

int spawn_hidden_count(void)
{
    return g_hidden;
}

/* Collect the entities attached to the cells below <cell> whose resource name contains <pattern>
 * (any hide pattern if NULL). */
static int walk_cells(BYTE *cell, const char *pattern, BYTE **found, int max, int n, int depth)
{
    int t, i, kids;
    BYTE **kid;
    if (!cell || depth > 24 || !game_readable(cell, G->cell_pool + G->cell_ntypes * 0x14)) return n;
    for (t = 0; t < G->cell_ntypes; t++) {
        BYTE *arr = cell + G->cell_pool + t * 0x14, **el;
        int cnt = *(int *)(arr + NARRAY_NUM);
        el = *(BYTE ***)(arr + NARRAY_ELEMS);
        if (cnt <= 0 || cnt > 200000 || !game_readable(el, cnt * 4)) continue;
        for (i = 0; i < cnt && n < max; i++) {
            const char *name;
            if (!el[i] || !game_readable(el[i] + G->entity_resname, NSTRING_SIZE)) continue;
            name = nstring_get(el[i] + G->entity_resname);
            if (pattern ? strstr(name, pattern) != NULL : name_hidden(name)) found[n++] = el[i];
        }
    }
    kids = *(int *)(cell + G->cell_children);
    kid = *(BYTE ***)(cell + G->cell_children + 4);
    if (kids > 0 && kids <= 64 && game_readable(kid, kids * 4))
        for (i = 0; i < kids; i++) n = walk_cells(kid[i], pattern, found, max, n, depth + 1);
    return n;
}

static BYTE *root_cell(void)
{
    BYTE *lvl = spawn_current_level();
    if (!lvl || !G->cell_pool || !game_readable(lvl + LEVEL_ROOTCELL, 4)) return NULL;
    return *(BYTE **)(lvl + LEVEL_ROOTCELL);
}

/* deferred: detach the entities of the current level that match the hide patterns */
static void do_hide_attached(const char *a, const char *b)
{
    static BYTE *found[4096];
    BYTE *lvl = spawn_current_level();
    int n, i;
    (void)a;
    (void)b;
    if (!lvl) return;
    n = walk_cells(root_cell(), NULL, found, 4096, 0, 0);
    for (i = 0; i < n; i++)
        if (*(BYTE **)(found[i] + ENTITY_CELL)) ((PFN_ThisPtr)ADDR_LEVEL_REMOVEENTITY)(lvl, NULL, found[i]);
    g_hidden += n;
    g_nhide = 0;   /* one-shot: do not filter later level loads */
    mlog("spawn: detached %d attached entities matching the hide patterns", n);
}

/* Hide already attached entities of the current level by the given patterns (in addition to the
 * load-time filter, for when the base level was not reloaded). */
int spawn_hide_attached(const char (*patterns)[48], int n)
{
    int i, keep = g_hidden;
    spawn_set_hide(patterns, n);
    g_hidden = keep;
    if (available() && G->cell_pool && defer_call(do_hide_attached, "", "")) return 1;
    g_nhide = 0;
    (void)i;
    return 0;
}

int spawn_list_entities(const char *pattern, char *out, int outsz)
{
    static BYTE *found[4096];
    int n = walk_cells(root_cell(), pattern, found, 4096, 0, 0), i, len;
    len = snprintf(out, outsz, "%d entities match '%s'", n, pattern);
    for (i = 0; i < n && i < 12 && len < outsz - 120; i++)
        len += snprintf(out + len, outsz - len, "\n %p %s", found[i], nstring_get(found[i] + G->entity_resname));
    return len;
}

BYTE *spawn_current_level(void)
{
    BYTE *srv;
    if (!game_ok()) return NULL;
    srv = *(BYTE **)ADDR_GFXSERVER_PTR;
    if (!srv || !game_readable(srv + GFXSERVER_LEVEL, 4)) return NULL;
    return *(BYTE **)(srv + GFXSERVER_LEVEL);
}

/* Root cell bounding box of the current level (min xyz, max xyz). */
int level_bounds(float out[6])
{
    BYTE *lvl = spawn_current_level(), *root;
    if (!lvl || !game_readable(lvl + LEVEL_ROOTCELL, 4)) return 0;
    root = *(BYTE **)(lvl + LEVEL_ROOTCELL);
    if (!root || !game_readable(root + CELL_BBOX, 24)) return 0;
    memcpy(out, root + CELL_BBOX, 6 * sizeof(float));
    return 1;
}

static void release_all(int detach)
{
    BYTE *lvl = spawn_current_level();
    int i;
    for (i = 0; i < g_nents; i++) {
        BYTE *e = g_ents[i];
        if (detach && lvl && lvl == g_level && *(BYTE **)(e + ENTITY_CELL))
            ((PFN_ThisPtr)ADDR_LEVEL_REMOVEENTITY)(lvl, NULL, e);
        ((PFN_ThisVoid)ADDR_REFCOUNTED_RELEASE)(e, NULL);
    }
    if (g_nents) mlog("spawn: released %d entities (detach %d)", g_nents, detach);
    g_nents = 0;
    g_ntrees = 0;
    g_level = NULL;
}

static void create_model(const SpawnReq *q)
{
    BYTE util[16], res[NSTRING_SIZE], arr[64];
    int n, i;
    BYTE **el;
    memset(util, 0, sizeof util);
    memset(arr, 0, sizeof arr);
    nstring_make(res, q->res);
    ((PFN_SegCreate)ADDR_SEGGFX_CREATE)(util, NULL, arr, res, q->m, 1);
    n = *(int *)(arr + NARRAY_NUM);
    el = *(BYTE ***)(arr + NARRAY_ELEMS);
    for (i = 0; i < n && g_nents < MAX_SPAWNED; i++) {
        BYTE *e = el[i];
        if (!e) continue;
        (*(int *)(e + REFCOUNT))++;   /* our reference */
        g_ents[g_nents++] = e;
    }
    ((PFN_ThisVoid)ADDR_NARRAY_PTR_DTOR)(arr, NULL);
    nstring_free(res);
}

static void create_tree(const SpawnReq *q)
{
    BYTE *mgr = *(BYTE **)G->forest_mgr_ptr;
    BYTE id[NSTRING_SIZE], file[NSTRING_SIZE], phys[NSTRING_SIZE];
    char name[32];
    if (!mgr) return;
    snprintf(name, sizeof name, "drakensang_mod_tree_%d", ++g_treeSerial);
    nstring_make(id, name);
    nstring_make(file, q->res);
    nstring_make(phys, "");
    ((PFN_AddTree)G->forest_add_tree)(mgr, NULL, id, file, phys, q->m, q->bright, q->nearLod, q->farLod);
    nstring_free(id);
    nstring_free(file);
    nstring_free(phys);
    g_ntrees++;
}

/* deferred: runs outside the render frame */
static void do_spawn(const char *a, const char *b)
{
    BYTE *lvl = spawn_current_level();
    int k;
    (void)a;
    (void)b;
    g_pumpQueued = 0;
    if (!lvl || lvl != g_qlevel) {   /* the level changed since the requests were queued */
        g_qcount = 0;
        return;
    }
    if (level_loading() || level_change_pending() || level_travel_queued()) {   /* never during a level change */
        g_pumpQueued = defer_call(do_spawn, "", "");
        return;
    }
    if (g_level && g_level != lvl) release_all(0);
    g_level = lvl;
    for (k = 0; k < PER_PUMP && g_qcount; k++) {
        SpawnReq *q = &g_queue[g_qhead];
        g_qhead = (g_qhead + 1) % MAX_QUEUE;
        g_qcount--;
        if (q->tree) create_tree(q);
        else create_model(q);
    }
    if (!g_qcount) mlog("spawn: queue done, %d entities, %d trees", g_nents, g_ntrees);
    if (g_qcount) g_pumpQueued = defer_call(do_spawn, "", "");
}

static SpawnReq *enqueue(void)
{
    SpawnReq *q;
    if (!g_queue) g_queue = (SpawnReq *)HeapAlloc(GetProcessHeap(), 0, MAX_QUEUE * sizeof(SpawnReq));
    if (!g_queue || g_qcount >= MAX_QUEUE) return NULL;
    if (g_qcount && g_qlevel != spawn_current_level()) g_qcount = 0;
    g_qlevel = spawn_current_level();
    q = &g_queue[(g_qhead + g_qcount) % MAX_QUEUE];
    memset(q, 0, sizeof *q);
    g_qcount++;
    return q;
}

static int kick(void)
{
    if (!g_pumpQueued) g_pumpQueued = defer_call(do_spawn, "", "");
    return g_pumpQueued;
}

int spawn_model(const char *res, const float m[16])
{
    SpawnReq *q;
    if (!available() || !spawn_current_level() || !(q = enqueue())) return 0;
    strncpy(q->res, res, sizeof q->res - 1);
    memcpy(q->m, m, sizeof q->m);
    return kick();
}

/* SpeedTree <file> (relative to export:trees/, without .spt) with the given LOD and brightness. */
int spawn_tree(const char *file, const float m[16], float nearLod, float farLod, const float bright[4])
{
    SpawnReq *q;
    if (!available() || !g_treeOk || !spawn_current_level() || !(q = enqueue())) return 0;
    strncpy(q->res, file, sizeof q->res - 1);
    memcpy(q->m, m, sizeof q->m);
    q->nearLod = nearLod;
    q->farLod = farLod;
    memcpy(q->bright, bright, sizeof q->bright);
    q->tree = 1;
    return kick();
}

int spawn_pending(void)
{
    return g_qcount;
}

static void do_clear(const char *a, const char *b)
{
    (void)a;
    (void)b;
    release_all(1);
}

int spawn_clear(void)
{
    g_qcount = 0;
    if (!g_nents) return 0;
    return defer_call(do_clear, "", "");
}

int spawn_info(char *out, int outsz)
{
    BYTE *lvl = spawn_current_level();
    int i, len = snprintf(out, outsz, "level %p (spawned into %p), %d entities, %d trees, %d queued, %d hidden",
                          lvl, g_level, g_nents, g_ntrees, g_qcount, g_hidden);
    for (i = 0; i < g_nents && i < 8 && len < outsz - 200; i++) {
        BYTE *e = g_ents[i];
        DWORD *d = (DWORD *)e;
        len += snprintf(out + len, outsz - len, "\n e=%p vt=%08lx rc=%ld +08=%08lx +0c=%08lx +10=%08lx +14=%08lx +18=%08lx",
                        e, d[0], (long)d[1], d[2], d[3], d[4], d[5], d[6]);
    }
    return len;
}

int spawn_count(void)
{
    return g_nents;
}

/* per frame: drop our references once the level they belonged to is gone */
void spawn_frame(void)
{
    if (g_nents && spawn_current_level() != g_level) defer_call(do_clear, "", "");
}

void spawn_shutdown(void)
{
    g_qcount = 0;
    g_nhide = 0;
    release_all(1);   /* called from Present during unload: removal is a plain list operation */
}
