/* Unlimited draw distance: while enabled, the per-entity LOD visibility test runs with
 * max distance = infinite and min screen size = 0. The entity's own values are restored right
 * after the call, so nothing is changed permanently. Visibility zones (VisibilityClient checks)
 * can be reported as always visible, and fog distances scaled or pushed out of sight.
 *
 * Models themselves contain nLodNodes: a child per detail level chosen by distance thresholds, and
 * nothing at all beyond the node's max distance (60-150 m for most buildings and props). While
 * enabled, the max distance is lifted (except for the <= 2 m "never visible" nodes used for hidden
 * states of destructible objects) and the thresholds are scaled by model_detail_distance; both are
 * restored right after the node is processed. The camera far plane (2500-3000 m) can be raised.
 *
 * Fog reaches the shaders differently per game: Drakensang (2008) sets the FogDistances shader
 * variable through nVariableServer when it changes; The River of Time builds FogDistances[2]
 * every frame from two fog layers (near/far fields of its atmosphere object). */
#include "game.h"

typedef BYTE (__fastcall *PFN_Test)(BYTE *self, void *edx);
static Detour d_entity, d_light, d_vis[2];
static PFN_Test orig_entity, orig_light, orig_vis[2];
static int g_ok = -1, g_visOk = -1, g_lodOk = -1;
int g_farObjects = 1, g_farLights = 1, g_ignoreZones = 1, g_farModels = 1;
float g_lodDetail = 1.0f, g_farClip = 0.0f;

/* model LOD nodes */
static Detour d_lodnode;
static BYTE (__fastcall *orig_lodnode)(BYTE *self, void *edx, void *sceneServer, void *renderContext);
#define MAX_THRESHOLDS 8

/* SpeedTree types: LOD limits multiplied when a tree type is loaded */
static Detour d_treeload;
static void *(__fastcall *orig_treeload)(void *self, void *edx, void *a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5,
                                          float nearLod, float farLod);
float g_treeDetail = 1.0f;
static int g_treeOk = -1;

static void *__fastcall hk_treeload(void *self, void *edx, void *a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5,
                                    float nearLod, float farLod)
{
    if (g_treeDetail > 0.0f && g_treeDetail != 1.0f && nearLod > 0.0f && farLod > 0.0f) {
        nearLod *= g_treeDetail;
        farLod *= g_treeDetail;
    }
    return orig_treeload(self, edx, a1, a2, a3, a4, a5, nearLod, farLod);
}

/* camera far plane: original value of the camera we changed */
static BYTE *g_clipCam;
static float g_clipOrig;

/* fog: distances multiplied by g_fogScale while g_fogEnabled, removed entirely otherwise */
static Detour d_setvec;
static void (__fastcall *orig_setvec)(BYTE *self, void *edx, int handle, const float *v);
static int g_fogOk = -1, g_fogHandle = -1, g_haveFog;
static float g_lastFog[4];
static Detour d_fogfunc;
static void (__fastcall *orig_fogfunc)(BYTE *self, void *edx);
static LONG g_fogCalls;
float g_fogScale = 1.0f;
int g_fogEnabled = 1;

static void fog_adjust(const float *in, float *out)
{
    memcpy(out, in, 4 * sizeof(float));
    if (!g_fogEnabled || g_fogScale <= 0.0f) {
        out[0] = 1.0e7f;
        out[1] = 2.0e7f;
        out[3] = 1.0e7f;
    } else {
        out[0] *= g_fogScale;
        out[1] *= g_fogScale;
        out[3] *= g_fogScale;
    }
}

static void __fastcall hk_setvec(BYTE *self, void *edx, int handle, const float *v)
{
    if (handle == g_fogHandle && v) {
        float t[4];
        memcpy(g_lastFog, v, sizeof g_lastFog);
        g_haveFog = 1;
        fog_adjust(v, t);
        orig_setvec(self, edx, handle, t);
        return;
    }
    orig_setvec(self, edx, handle, v);
}

/* The River of Time: adjust the layers' near/far fields around the call that uploads them. */
static void __fastcall hk_fogfunc(BYTE *self, void *edx)
{
    float save[2][2];
    int k;
    g_fogCalls++;
    for (k = 0; k < 2; k++) {
        float *n = (float *)(self + G->fog_near[k]), *f = (float *)(self + G->fog_far[k]);
        save[k][0] = *n, save[k][1] = *f;
        g_lastFog[k * 2] = *n, g_lastFog[k * 2 + 1] = *f;
        if (!g_fogEnabled || g_fogScale <= 0.0f) {
            *n = 1.0e7f;
            *f = 2.0e7f;
        } else {
            *n *= g_fogScale;
            *f *= g_fogScale;
        }
    }
    g_haveFog = 1;
    orig_fogfunc(self, edx);
    for (k = 0; k < 2; k++) {
        *(float *)(self + G->fog_near[k]) = save[k][0];
        *(float *)(self + G->fog_far[k]) = save[k][1];
    }
}

/* Push the current fog setting immediately (the game only sets it when its value changes). */
void fog_apply(void)
{
    BYTE *vs;
    float t[4];
    if (g_fogOk <= 0 || !g_haveFog || G->fog_mode != FOG_VARSERVER) return;   /* per-frame otherwise */
    vs = *(BYTE **)ADDR_VARSERVER_PTR;
    if (!vs) return;
    fog_adjust(g_lastFog, t);
    orig_setvec(vs, NULL, g_fogHandle, t);
}

static BYTE visible_far(BYTE (__fastcall *fn)(BYTE *, void *), BYTE *self)
{
    float *maxd = (float *)(self + ENT_LOD_MAXDIST), *minsz = (float *)(self + ENT_LOD_MINSIZE);
    float omax = *maxd, osz = *minsz;
    BYTE r;
    *maxd = 1.0e9f;
    *minsz = 0.0f;
    r = fn(self, NULL);
    *maxd = omax;
    *minsz = osz;
    return r;
}

static BYTE __fastcall hk_entity(BYTE *self, void *edx)
{
    return g_farObjects ? visible_far(orig_entity, self) : orig_entity(self, edx);
}

static BYTE __fastcall hk_light(BYTE *self, void *edx)
{
    return g_farLights ? visible_far(orig_light, self) : orig_light(self, edx);
}

static BYTE __fastcall hk_lodnode(BYTE *self, void *edx, void *sceneServer, void *renderContext)
{
    float *maxd = (float *)(self + G->lodnode_maxdist), omax = *maxd, save[MAX_THRESHOLDS];
    BYTE *arr = self + G->lodnode_thresholds;
    int n = 0, i, n2;
    float *th;
    BYTE r;
    if (!g_farModels && g_lodDetail == 1.0f) return orig_lodnode(self, edx, sceneServer, renderContext);
    if (g_farModels && omax > 2.0f) *maxd = 1.0e9f;
    if (g_lodDetail != 1.0f) {
        n = *(int *)(arr + NARRAY_NUM);
        th = *(float **)(arr + NARRAY_ELEMS);
        if (n < 0 || n > MAX_THRESHOLDS || (n && !th)) n = 0;
        for (i = 0; i < n; i++) {
            save[i] = th[i];
            th[i] *= g_lodDetail;
        }
    }
    r = orig_lodnode(self, edx, sceneServer, renderContext);
    if (n) {
        /* the node may have appended thresholds (and reallocated the array) on its first render */
        n2 = *(int *)(arr + NARRAY_NUM);
        th = *(float **)(arr + NARRAY_ELEMS);
        for (i = 0; i < n2 && th; i++) th[i] = i < n ? save[i] : th[i] / g_lodDetail;
    }
    *maxd = omax;
    return r;
}

/* Camera far plane: raise it on the current camera (the game sets it once per level). */
static void far_clip_apply(void)
{
    BYTE *cam = cam_main(), *nc;
    float *farp;
    if (g_clipCam && g_clipCam != cam) g_clipCam = NULL;   /* level changed: that camera is gone */
    if (!cam) return;
    nc = cam + CAM_NCAMERA;
    farp = (float *)(nc + G->ncam_far);
    if (g_farClip > 0.0f) {
        if (!g_clipCam) {
            g_clipCam = cam;
            g_clipOrig = *farp;
        }
        if (*farp == g_farClip) return;
        *farp = g_farClip;
    } else {
        if (!g_clipCam) return;
        *farp = g_clipOrig;
        g_clipCam = NULL;
    }
    nc[NCAM_PROJ_DIRTY] = 1;
    cam[CAM_VIEWPROJ_DIRTY] = 1;
}

void drawdist_frame(void)
{
    if (g_ok > 0) far_clip_apply();
}

void drawdist_shutdown(void)
{
    float keep = g_farClip;
    g_farClip = 0.0f;
    if (g_ok > 0) far_clip_apply();
    g_farClip = keep;
}

/* Visibility zones: report every zone as visible. The original still runs (it also updates
 * the client's zone list and statistics). */
static BYTE __fastcall hk_vis0(BYTE *self, void *edx)
{
    BYTE r = orig_vis[0](self, edx);
    return g_ignoreZones ? 1 : r;
}

static BYTE __fastcall hk_vis1(BYTE *self, void *edx)
{
    BYTE r = orig_vis[1](self, edx);
    return g_ignoreZones ? 1 : r;
}

static int install_fog(void)
{
    if (G->fog_mode == FOG_SHADERFUNC)
        return detour_install(&d_fogfunc, G->fog_func, G->pro_fog_func, (void *)hk_fogfunc, (void **)&orig_fogfunc);
    if (G->fog_mode == FOG_VARSERVER) {
        BYTE *vs = *(BYTE **)ADDR_VARSERVER_PTR;
        if (!vs) return 0;
        g_fogHandle = ((int (__fastcall *)(BYTE *, void *, const char *))ADDR_VAR_GETHANDLE)(vs, NULL, "FogDistances");
        return detour_install(&d_setvec, ADDR_VAR_SETVECTOR, PRO_VAR_SETVECTOR, (void *)hk_setvec,
                              (void **)&orig_setvec);
    }
    return 0;
}

void drawdist_init(void)
{
    static void *const vis_hooks[2] = {(void *)hk_vis0, (void *)hk_vis1};
    g_farObjects = g_cfg.far_objects;
    g_farLights = g_cfg.far_lights;
    g_ignoreZones = g_cfg.ignore_zones;
    g_fogScale = g_cfg.fog_scale;
    g_fogEnabled = g_fogScale > 0.0f;
    g_farModels = g_cfg.far_models;
    g_lodDetail = g_cfg.lod_detail > 0.0f ? g_cfg.lod_detail : 1.0f;
    g_farClip = g_cfg.far_clip;
    g_treeDetail = g_cfg.tree_detail;
    if (g_ok >= 0 || !game_ok()) return;
    if (g_treeOk < 0) {
        g_treeOk = G->tree_load &&
                   detour_install(&d_treeload, G->tree_load, G->pro_tree_load, (void *)hk_treeload, (void **)&orig_treeload);
        mlog("tree LOD distance: %s (x%.2f, from the next level load)", g_treeOk ? "available" : "DISABLED", g_treeDetail);
    }
    if (g_lodOk < 0) {
        g_lodOk = G->lodnode_render &&
                  detour_install(&d_lodnode, G->lodnode_render, G->pro_lodnode_render, (void *)hk_lodnode,
                                 (void **)&orig_lodnode);
        mlog("model LOD distance: %s (unlimited %d, detail x%.2f)", g_lodOk ? "available" : "DISABLED", g_farModels,
             g_lodDetail);
    }
    if (g_visOk < 0) {
        int k;
        g_visOk = G->vis[0] != 0;
        for (k = 0; k < 2 && g_visOk; k++)
            if (G->vis[k] && !detour_install(&d_vis[k], G->vis[k], G->pro_vis[k], vis_hooks[k], (void **)&orig_vis[k]))
                g_visOk = 0;
        mlog("visibility zones bypass: %s (%d)", g_visOk ? "available" : "DISABLED", g_ignoreZones);
    }
    if (g_fogOk < 0) {
        g_fogOk = install_fog();
        mlog("fog control: %s (mode %d, handle %d, scale %.2f)", g_fogOk ? "available" : "DISABLED", G->fog_mode,
             g_fogHandle, g_fogScale);
    }
    g_ok = detour_install(&d_entity, ADDR_ENTITY_LODVISIBLE, PRO_ENTITY_LODVISIBLE, (void *)hk_entity,
                          (void **)&orig_entity) &&
           detour_install(&d_light, ADDR_LIGHT_LODVISIBLE, PRO_LIGHT_LODVISIBLE, (void *)hk_light,
                          (void **)&orig_light);
    mlog("draw distance: %s (objects %d, lights %d)", g_ok ? "available" : "DISABLED", g_farObjects, g_farLights);
}

int fog_toggle(void)
{
    if (g_fogOk <= 0) return -1;
    g_fogEnabled = !g_fogEnabled;
    if (g_fogEnabled && g_fogScale <= 0.0f) g_fogScale = 1.0f;
    fog_apply();
    return g_fogEnabled;
}

int drawdist_command(const char *cmd, const char *arg, char *out, int outsz)
{
    int *target;
    if (!_stricmp(cmd, "fog")) {
        if (!_stricmp(arg, "off")) g_fogEnabled = 0;
        else if (!_stricmp(arg, "on")) g_fogEnabled = 1;
        else if (arg[0]) g_fogScale = (float)atof(arg), g_fogEnabled = g_fogScale > 0.0f;
        fog_apply();
        snprintf(out, outsz, "fog enabled=%d scale=%.2f handle=%d last=(%.1f %.1f %.1f %.1f) hook=%d calls=%ld",
                 g_fogEnabled, g_fogScale, g_fogHandle, g_lastFog[0], g_lastFog[1], g_lastFog[2], g_lastFog[3], g_fogOk,
                 g_fogCalls);
        return 1;
    }
    if (!_stricmp(cmd, "farclip")) {   /* farclip <metres> (0 = game default) */
        if (arg[0]) g_farClip = (float)atof(arg);
        {
            BYTE *cam = cam_main();
            snprintf(out, outsz, "far clip setting %.0f, camera near %.3f far %.1f", g_farClip,
                     cam ? *(float *)(cam + CAM_NCAMERA + G->ncam_near) : 0.0f,
                     cam ? *(float *)(cam + CAM_NCAMERA + G->ncam_far) : 0.0f);
        }
        return 1;
    }
    if (!_stricmp(cmd, "lodmodels") || !_stricmp(cmd, "loddetail")) {   /* lodmodels on|off, loddetail <x> */
        if (!_stricmp(cmd, "loddetail") && arg[0]) g_lodDetail = (float)atof(arg) > 0.0f ? (float)atof(arg) : 1.0f;
        if (!_stricmp(cmd, "lodmodels")) {
            if (!_stricmp(arg, "on") || !strcmp(arg, "1")) g_farModels = 1;
            else if (!_stricmp(arg, "off") || !strcmp(arg, "0")) g_farModels = 0;
            else if (!_stricmp(arg, "toggle")) g_farModels = !g_farModels;
        }
        snprintf(out, outsz, "model LOD: unlimited=%d detail x%.2f (hook %s)", g_farModels, g_lodDetail,
                 g_lodOk > 0 ? "ok" : "missing");
        return 1;
    }
    if (!_stricmp(cmd, "treedetail")) {   /* treedetail <x>: applies to trees loaded from now on */
        if (arg[0]) g_treeDetail = (float)atof(arg);
        snprintf(out, outsz, "tree LOD distance x%.2f (hook %s; reload the level to apply)", g_treeDetail,
                 g_treeOk > 0 ? "ok" : "missing");
        return 1;
    }
    if (!_stricmp(cmd, "lod")) target = &g_farObjects;
    else if (!_stricmp(cmd, "lodlights")) target = &g_farLights;
    else if (!_stricmp(cmd, "zones")) target = &g_ignoreZones;
    else return 0;
    if (!_stricmp(arg, "on") || !strcmp(arg, "1")) *target = 1;
    else if (!_stricmp(arg, "off") || !strcmp(arg, "0")) *target = 0;
    else if (!_stricmp(arg, "toggle")) *target = !*target;
    snprintf(out, outsz, "far objects=%d far lights=%d ignore zones=%d (hooks %s/%s)", g_farObjects, g_farLights,
             g_ignoreZones, g_ok > 0 ? "ok" : "missing", g_visOk > 0 ? "ok" : "missing");
    return 1;
}
