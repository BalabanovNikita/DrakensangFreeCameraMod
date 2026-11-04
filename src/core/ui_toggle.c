/* Hide / show the whole game UI.
 *
 * Drakensang (2008): the game renders all UI windows offscreen into GuiLayer1..3 and blends them
 * over the scene in the render path pass "compose_with_gui". Its twin pass "compose" produces the
 * same frame without the GUI, and the "gui" pass draws Nebula's nGui. UI::Server::Open enables
 * compose_with_gui and disables compose once; we flip them (and gui) while the UI is hidden and
 * re-assert the flags every frame because other game code saves/restores pass states.
 *
 * The River of Time: PostEffectsServer::SetComposeMode(mode) enables one of the passes compose,
 * compose_gui, compose_dof, compose_gui_dof (bit 0 = with GUI, bit 1 = depth of field). A detour
 * strips the GUI bit while hidden and remembers the mode the game asked for, to restore it. */
#include "game.h"

static int g_ok = -1;

typedef struct {
    BYTE *withGui, *noGui, *gui;
} UiPasses;

/* original enabled flags, captured when hiding */
static BYTE g_origWithGui = 1, g_origNoGui = 0, g_origGui = 1;

/* compose mode path */
static Detour d_compose;
static void (__fastcall *orig_compose)(BYTE *self, void *edx, int mode);
static int g_wantMode = -1;   /* last mode requested by the game */

static void __fastcall hk_compose(BYTE *self, void *edx, int mode)
{
    g_wantMode = mode;
    if (g_uiHidden) mode &= ~1;
    orig_compose(self, edx, mode);
}

static int available(void)
{
    if (g_ok < 0) {
        g_ok = game_ok();
        if (g_ok && G->ui_mode == UI_COMPOSEMODE)
            g_ok = detour_install(&d_compose, G->set_compose_mode, G->pro_set_compose_mode, (void *)hk_compose,
                                  (void **)&orig_compose);
        mlog("ui toggle: %s", g_ok ? "available" : "DISABLED");
    }
    return g_ok;
}

static BYTE *passes(BYTE *section, int *count)
{
    int n = *(int *)(section + SECTION_PASSES + NARRAY_NUM);
    BYTE *p = *(BYTE **)(section + SECTION_PASSES + NARRAY_ELEMS);
    *count = 0;
    if (n <= 0 || n > 128 || !game_readable(p, n * PASS_STRIDE)) return NULL;
    *count = n;
    return p;
}

static BYTE *find_pass(BYTE *section, const char *name)
{
    int np, j;
    BYTE *ps = passes(section, &np);
    for (j = 0; j < np; j++)
        if (!strcmp(nstring_get(ps + j * PASS_STRIDE + PASS_NAME), name)) return ps + j * PASS_STRIDE;
    return NULL;
}

/* ---- Drakensang (2008): render path sections of the scene server ---------- */
static BYTE *sections(int *count)
{
    BYTE *scene, *rp, *secs;
    int n;
    *count = 0;
    scene = *(BYTE **)ADDR_SCENESERVER_PTR;
    if (!scene || !game_readable(scene, 0x300)) return NULL;
    rp = scene + SCENE_RENDERPATH;
    n = *(int *)(rp + RP_SECTIONS + NARRAY_NUM);
    secs = *(BYTE **)(rp + RP_SECTIONS + NARRAY_ELEMS);
    if (n <= 0 || n > 64 || !game_readable(secs, n * SECTION_STRIDE)) return NULL;
    *count = n;
    return secs;
}

/* The section that contains compose_with_gui (the main "default" section). */
static int find_ui_passes(UiPasses *up)
{
    int ns, i;
    BYTE *secs = sections(&ns);
    memset(up, 0, sizeof *up);
    for (i = 0; i < ns; i++) {
        BYTE *s = secs + i * SECTION_STRIDE;
        UiPasses cand = {find_pass(s, "compose_with_gui"), find_pass(s, "compose"), find_pass(s, "gui")};
        if (cand.withGui && cand.noGui) {
            *up = cand;
            return 1;
        }
    }
    return 0;
}

static void apply_d1(int hidden)
{
    UiPasses up;
    if (!find_ui_passes(&up)) return;
    if (hidden) {
        up.withGui[PASS_ENABLED] = 0;
        up.noGui[PASS_ENABLED] = 1;
        if (up.gui) up.gui[PASS_ENABLED] = 0;
    } else {
        up.withGui[PASS_ENABLED] = g_origWithGui;
        up.noGui[PASS_ENABLED] = g_origNoGui;
        if (up.gui) up.gui[PASS_ENABLED] = g_origGui;
    }
}

static int set_hidden_d1(int hidden)
{
    UiPasses up;
    if (!find_ui_passes(&up)) {
        mlog("ui toggle: render passes not found");
        return 0;
    }
    if (hidden) {
        g_origWithGui = up.withGui[PASS_ENABLED];
        g_origNoGui = up.noGui[PASS_ENABLED];
        g_origGui = up.gui ? up.gui[PASS_ENABLED] : 1;
        /* if the game was in an odd state, restore to the normal in-game setup later */
        if (!g_origWithGui && g_origNoGui) {
            g_origWithGui = 1;
            g_origNoGui = 0;
        }
    }
    g_uiHidden = hidden;
    apply_d1(hidden);
    mlog("ui %s (passes %p %p %p)", hidden ? "hidden" : "shown", up.withGui, up.noGui, up.gui);
    return 1;
}

/* ---- The River of Time: post effects server compose mode ----------------- */
static BYTE *pe_server(void)
{
    BYTE *pe = *(BYTE **)G->posteffects_ptr;
    if (!pe || !game_readable(pe, G->pe_mode + 4) || !*(BYTE **)(pe + G->pe_section)) return NULL;
    return pe;
}

static void apply_compose(int hidden)
{
    BYTE *pe = pe_server(), *gui;
    int mode;
    if (!pe) return;
    mode = *(int *)(pe + G->pe_mode);
    if (hidden && (mode & 1)) orig_compose(pe, NULL, mode & ~1);
    else if (!hidden && g_wantMode >= 0 && mode != g_wantMode) orig_compose(pe, NULL, g_wantMode);
    gui = find_pass(*(BYTE **)(pe + G->pe_section), "gui");
    if (gui) gui[PASS_ENABLED] = hidden ? 0 : g_origGui;
}

static int set_hidden_compose(int hidden)
{
    BYTE *pe = pe_server(), *gui;
    if (!pe) {
        mlog("ui toggle: post effects server not ready");
        return 0;
    }
    if (hidden) {
        int mode = *(int *)(pe + G->pe_mode);
        if (mode >= 0 && mode <= 3) g_wantMode = mode;
        if (g_wantMode < 0) g_wantMode = 1;
        gui = find_pass(*(BYTE **)(pe + G->pe_section), "gui");
        g_origGui = gui ? gui[PASS_ENABLED] : 1;
    }
    g_uiHidden = hidden;
    apply_compose(hidden);
    mlog("ui %s (compose mode now %d, game wants %d)", hidden ? "hidden" : "shown", *(int *)(pe + G->pe_mode),
         g_wantMode);
    return 1;
}

int ui_set_hidden(int hidden)
{
    if (hidden == g_uiHidden) return 1;
    if (!available()) return 0;
    return G->ui_mode == UI_COMPOSEMODE ? set_hidden_compose(hidden) : set_hidden_d1(hidden);
}

void ui_frame(void)
{
    if (!g_uiHidden || g_ok <= 0) return;
    if (G->ui_mode == UI_COMPOSEMODE) apply_compose(1);
    else apply_d1(1);
}

static int list_section(BYTE *s, const char *name, char *out, int outsz)
{
    int np, j, len = snprintf(out, outsz, "[%s]", name);
    BYTE *ps = passes(s, &np);
    for (j = 0; j < np && len < outsz - 60; j++) {
        BYTE *p = ps + j * PASS_STRIDE;
        len += snprintf(out + len, outsz - len, " %s=%d", nstring_get(p + PASS_NAME), p[PASS_ENABLED]);
    }
    return len + snprintf(out + len, outsz - len, "\n");
}

/* debug: list render path passes */
int ui_list_passes(char *out, int outsz)
{
    int ns, i, len = 0;
    BYTE *secs;
    if (!available()) return snprintf(out, outsz, "render path not available");
    if (G->ui_mode == UI_COMPOSEMODE) {
        BYTE *pe = pe_server();
        if (!pe) return snprintf(out, outsz, "post effects server not ready");
        len = snprintf(out, outsz, "compose mode %d (game wants %d)\n", *(int *)(pe + G->pe_mode), g_wantMode);
        return len + list_section(*(BYTE **)(pe + G->pe_section), "post effects section", out + len, outsz - len);
    }
    secs = sections(&ns);
    if (!secs) return snprintf(out, outsz, "render path not available");
    for (i = 0; i < ns && len < outsz - 80; i++) {
        BYTE *s = secs + i * SECTION_STRIDE;
        len += list_section(s, nstring_get(s + SECTION_NAME), out + len, outsz - len);
    }
    return len;
}
