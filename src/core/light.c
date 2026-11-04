/* Lighting panel: override the scene's sun / moon light, ambient and back light, shadows, color
 * grading, bloom, fog and sky.
 *
 * Mangalore's PostEffects::Server blends the parameters of the post effect zones ("ambience
 * bubbles") around the camera into one parameter block and pushes it every frame, in
 * ApplyParameters(), to the shader variables (Saturation, Balance, HdrBloom*, Fog*), to the global
 * light entity (colors, intensities, transform = light direction) and to the sky. A detour on
 * ApplyParameters writes the overridden values into the block, lets the game apply them and
 * restores the block, so the game's own state and zone blending stay untouched.
 *
 * Overrides stay active across level changes until reset. Presets: <game>\drakensang_mod_lighting.ini. */
#include "game.h"

enum { K_HEADER, K_FLOAT, K_MUL, K_INVALPHA, K_YAW, K_PITCH };

typedef struct LItem {
    const char *id;       /* commands and presets */
    const char *label;
    int   kind;
    int   off;            /* offset in the post effects server (K_MUL: first of 3 floats) */
    float step, lo, hi;
} LItem;

/* Drakensang (2008): PostEffects::Server, block at +0x70. Light color is premultiplied by intensity. */
static const LItem g_itemsD1[] = {
    {NULL, "Sun / moon", K_HEADER},
    {"sun_yaw", "Direction (compass)", K_YAW, 0x70, 2.0f, -180.0f, 180.0f},
    {"sun_pitch", "Direction (height)", K_PITCH, 0x70, 2.0f, -89.0f, 89.0f},
    {"sun_bright", "Brightness x", K_MUL, 0x114, 0.05f, 0.0f, 10.0f},
    {"sun_r", "Color red", K_FLOAT, 0x114, 0.02f, 0.0f, 5.0f},
    {"sun_g", "Color green", K_FLOAT, 0x118, 0.02f, 0.0f, 5.0f},
    {"sun_b", "Color blue", K_FLOAT, 0x11c, 0.02f, 0.0f, 5.0f},
    {"shadow", "Shadow strength", K_FLOAT, 0xe0, 0.05f, 0.0f, 1.0f},
    {NULL, "Ambient and back light", K_HEADER},
    {"amb_bright", "Ambient brightness x", K_MUL, 0x134, 0.05f, 0.0f, 20.0f},
    {"amb_r", "Ambient red", K_FLOAT, 0x134, 0.01f, 0.0f, 3.0f},
    {"amb_g", "Ambient green", K_FLOAT, 0x138, 0.01f, 0.0f, 3.0f},
    {"amb_b", "Ambient blue", K_FLOAT, 0x13c, 0.01f, 0.0f, 3.0f},
    {"back_factor", "Back light strength", K_FLOAT, 0xd4, 0.05f, 0.0f, 3.0f},
    {"back_r", "Back light red", K_FLOAT, 0x124, 0.02f, 0.0f, 3.0f},
    {"back_g", "Back light green", K_FLOAT, 0x128, 0.02f, 0.0f, 3.0f},
    {"back_b", "Back light blue", K_FLOAT, 0x12c, 0.02f, 0.0f, 3.0f},
    {NULL, "Image", K_HEADER},
    {"saturation", "Saturation", K_FLOAT, 0xb4, 0.05f, 0.0f, 3.0f},
    {"tint_r", "Tint red", K_FLOAT, 0xf4, 0.02f, 0.0f, 3.0f},
    {"tint_g", "Tint green", K_FLOAT, 0xf8, 0.02f, 0.0f, 3.0f},
    {"tint_b", "Tint blue", K_FLOAT, 0xfc, 0.02f, 0.0f, 3.0f},
    {"bloom", "Bloom strength", K_FLOAT, 0xb8, 0.05f, 0.0f, 5.0f},
    {"bloom_threshold", "Bloom threshold", K_FLOAT, 0xbc, 0.02f, 0.0f, 3.0f},
    {"bloom_r", "Bloom red", K_FLOAT, 0xe4, 0.02f, 0.0f, 3.0f},
    {"bloom_g", "Bloom green", K_FLOAT, 0xe8, 0.02f, 0.0f, 3.0f},
    {"bloom_b", "Bloom blue", K_FLOAT, 0xec, 0.02f, 0.0f, 3.0f},
    {NULL, "Fog and sky", K_HEADER},
    {"fog_start", "Fog start (m)", K_FLOAT, 0xc0, 5.0f, -500.0f, 20000.0f},
    {"fog_end", "Fog end (m)", K_FLOAT, 0xc4, 5.0f, 0.0f, 50000.0f},
    {"fog_height", "Fog height (m)", K_FLOAT, 0xc8, 10.0f, 0.0f, 10000.0f},
    {"fog_density", "Fog density", K_INVALPHA, 0x110, 0.05f, 0.0f, 1.0f},
    {"fog_r", "Fog red", K_FLOAT, 0x104, 0.02f, 0.0f, 3.0f},
    {"fog_g", "Fog green", K_FLOAT, 0x108, 0.02f, 0.0f, 3.0f},
    {"fog_b", "Fog blue", K_FLOAT, 0x10c, 0.02f, 0.0f, 3.0f},
    {"sky_bright", "Sky brightness", K_FLOAT, 0xdc, 0.05f, -1.0f, 3.0f},
    {"sky_contrast", "Sky contrast", K_FLOAT, 0xd8, 0.05f, 0.0f, 4.0f},
};
#define BLOCK_LO 0x70
#define BLOCK_HI 0x144   /* floats only; the rest of the block holds references */
#define MAX_ITEMS 64
#define VISIBLE_ROWS 18
#define DEG (3.14159265f / 180.0f)

static const LItem *g_items;
static int g_nitems;
static int g_set[MAX_ITEMS];
static float g_val[MAX_ITEMS];
static BYTE g_game[BLOCK_HI];   /* the game's values of the last frame (before overrides) */
static int g_haveGame;
static int g_open, g_sel, g_top, g_ok = -1;
static double g_repeatAt;
static int g_repeatVk;

static Detour d_apply;
static void (__fastcall *orig_apply)(BYTE *self, void *edx);

/* ---- direction ---------------------------------------------------------- */
static float vlen(const float *v)
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void dir_get(const BYTE *block, float *yaw, float *pitch)
{
    const float *z = (const float *)(block + 0x90);
    float l = vlen(z), y;
    if (l < 1e-6f) {
        *yaw = *pitch = 0.0f;
        return;
    }
    y = z[1] / l;
    if (y > 1.0f) y = 1.0f;
    if (y < -1.0f) y = -1.0f;
    *yaw = atan2f(z[0], z[2]) / DEG;
    *pitch = asinf(y) / DEG;
}

/* Rotation rows x, y, z of the matrix at block (keeping row lengths, handedness and position). */
static void dir_set(BYTE *block, float yawDeg, float pitchDeg)
{
    float *x = (float *)(block + 0x70), *y = (float *)(block + 0x80), *z = (float *)(block + 0x90);
    float lx = vlen(x), ly = vlen(y), lz = vlen(z), c[3], hand, nz[3], nx[3], ny[3], l;
    float cy = cosf(yawDeg * DEG), sy = sinf(yawDeg * DEG), cp = cosf(pitchDeg * DEG), sp = sinf(pitchDeg * DEG);
    int i;
    c[0] = x[1] * y[2] - x[2] * y[1], c[1] = x[2] * y[0] - x[0] * y[2], c[2] = x[0] * y[1] - x[1] * y[0];
    hand = c[0] * z[0] + c[1] * z[1] + c[2] * z[2] < 0.0f ? -1.0f : 1.0f;
    nz[0] = cp * sy, nz[1] = sp, nz[2] = cp * cy;
    nx[0] = nz[2], nx[1] = 0.0f, nx[2] = -nz[0];   /* up x z */
    l = vlen(nx);
    if (l < 1e-5f) nx[0] = 1.0f, nx[1] = nx[2] = 0.0f, l = 1.0f;
    for (i = 0; i < 3; i++) nx[i] /= l;
    ny[0] = nz[1] * nx[2] - nz[2] * nx[1], ny[1] = nz[2] * nx[0] - nz[0] * nx[2], ny[2] = nz[0] * nx[1] - nz[1] * nx[0];
    for (i = 0; i < 3; i++) {
        x[i] = nx[i] * lx;
        y[i] = ny[i] * ly * hand;
        z[i] = nz[i] * lz;
    }
}

/* ---- values -------------------------------------------------------------- */
/* The game's current value of item i (from the last frame). */
static float game_value(int i)
{
    const LItem *it = &g_items[i];
    float yaw, pitch;
    switch (it->kind) {
    case K_FLOAT: return *(const float *)(g_game + it->off);
    case K_MUL: return 1.0f;
    case K_INVALPHA: return 1.0f - *(const float *)(g_game + it->off);
    case K_YAW: dir_get(g_game, &yaw, &pitch); return yaw;
    case K_PITCH: dir_get(g_game, &yaw, &pitch); return pitch;
    }
    return 0.0f;
}

static float cur_value(int i)
{
    return g_set[i] ? g_val[i] : game_value(i);
}

static int any_set(void)
{
    int i;
    for (i = 0; i < g_nitems; i++)
        if (g_set[i]) return 1;
    return 0;
}

static void apply_overrides(BYTE *block)
{
    int i, yawSet = -1, pitchSet = -1;
    float yaw, pitch;
    for (i = 0; i < g_nitems; i++) {
        const LItem *it = &g_items[i];
        if (!g_set[i]) continue;
        if (it->kind == K_FLOAT) *(float *)(block + it->off) = g_val[i];
        else if (it->kind == K_INVALPHA) *(float *)(block + it->off) = 1.0f - g_val[i];
        else if (it->kind == K_YAW) yawSet = i;
        else if (it->kind == K_PITCH) pitchSet = i;
    }
    for (i = 0; i < g_nitems; i++) {   /* multipliers after the individual colors */
        const LItem *it = &g_items[i];
        if (g_set[i] && it->kind == K_MUL) {
            float *c = (float *)(block + it->off);
            c[0] *= g_val[i], c[1] *= g_val[i], c[2] *= g_val[i];
        }
    }
    if (yawSet >= 0 || pitchSet >= 0) {
        dir_get(block, &yaw, &pitch);
        dir_set(block, yawSet >= 0 ? g_val[yawSet] : yaw, pitchSet >= 0 ? g_val[pitchSet] : pitch);
    }
}

static void __fastcall hk_apply(BYTE *self, void *edx)
{
    BYTE save[BLOCK_HI - BLOCK_LO];
    memcpy(g_game + BLOCK_LO, self + BLOCK_LO, sizeof save);
    g_haveGame = 1;
    if (!any_set()) {
        orig_apply(self, edx);
        return;
    }
    memcpy(save, self + BLOCK_LO, sizeof save);
    apply_overrides(self);
    orig_apply(self, edx);
    memcpy(self + BLOCK_LO, save, sizeof save);
}

static int available(void)
{
    if (g_ok < 0) {
        g_ok = 0;
        if (game_ok() && G->pe_layout == PE_D1) {
            g_items = g_itemsD1;
            g_nitems = sizeof g_itemsD1 / sizeof g_itemsD1[0];
            g_ok = detour_install(&d_apply, G->pe_apply, G->pro_pe_apply, (void *)hk_apply, (void **)&orig_apply);
        }
        mlog("lighting: %s (%d items)", g_ok ? "available" : "DISABLED", g_nitems);
    }
    return g_ok;
}

void light_init(void)
{
    available();
}

static int find_item(const char *id)
{
    int i;
    for (i = 0; i < g_nitems; i++)
        if (g_items[i].id && !_stricmp(g_items[i].id, id)) return i;
    return -1;
}

static void set_value(int i, float v)
{
    const LItem *it = &g_items[i];
    if (it->kind == K_YAW) {
        while (v > 180.0f) v -= 360.0f;
        while (v < -180.0f) v += 360.0f;
    } else {
        if (v < it->lo) v = it->lo;
        if (v > it->hi) v = it->hi;
    }
    g_val[i] = v;
    g_set[i] = 1;
}

/* ---- presets ------------------------------------------------------------ */
static void preset_path(WCHAR *path)
{
    wcscpy(path, g_gameDir);
    wcscat(path, L"drakensang_mod_lighting.ini");
}

static int preset_save(const char *name)
{
    WCHAR path[MAX_PATH], sec[64], key[64], val[32];
    int i, n = 0;
    preset_path(path);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, sec, 64);
    WritePrivateProfileStringW(sec, NULL, NULL, path);   /* replace the section */
    for (i = 0; i < g_nitems; i++) {
        if (!g_set[i]) continue;
        char v[32];
        MultiByteToWideChar(CP_UTF8, 0, g_items[i].id, -1, key, 64);
        snprintf(v, sizeof v, "%.4f", g_val[i]);
        MultiByteToWideChar(CP_UTF8, 0, v, -1, val, 32);
        WritePrivateProfileStringW(sec, key, val, path);
        n++;
    }
    if (!n) WritePrivateProfileStringW(sec, L"empty", L"1", path);
    return n;
}

static int preset_load(const char *name)
{
    WCHAR path[MAX_PATH], sec[64], key[64], val[32];
    int i, n = 0;
    preset_path(path);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, sec, 64);
    if (!GetPrivateProfileSectionW(sec, val, 32, path)) return -1;   /* no such preset */
    for (i = 0; i < g_nitems; i++) {
        if (!g_items[i].id) continue;
        g_set[i] = 0;
        MultiByteToWideChar(CP_UTF8, 0, g_items[i].id, -1, key, 64);
        if (GetPrivateProfileStringW(sec, key, L"", val, 32, path) && val[0]) {
            char v[32];
            WideCharToMultiByte(CP_UTF8, 0, val, -1, v, sizeof v, NULL, NULL);
            set_value(i, (float)atof(v));
            n++;
        }
    }
    return n;
}

/* ---- panel -------------------------------------------------------------- */
int light_menu_open(void)
{
    return g_open;
}

void light_menu_set(int open)
{
    if (open && !available()) {
        toast("Lighting panel is not available in this game yet");
        return;
    }
    if (open && g_items[g_sel].kind == K_HEADER) g_sel++;
    g_open = open;
    g_menu.visible = open;
}

static int pressed_repeat(int vk)
{
    double now = now_seconds();
    if (input_key_pressed(vk)) {
        g_repeatVk = vk;
        g_repeatAt = now + 0.35;
        return 1;
    }
    if (g_repeatVk == vk && input_key_down(vk) && now >= g_repeatAt) {
        g_repeatAt = now + 0.04;
        return 1;
    }
    return 0;
}

static void move_sel(int dir)
{
    int i = g_sel;
    do i = (i + dir + g_nitems) % g_nitems;
    while (g_items[i].kind == K_HEADER);
    g_sel = i;
}

static void fill_panel(void)
{
    int i, n = 0;
    if (g_sel < g_top + 1) g_top = g_sel > 0 && g_items[g_sel - 1].kind == K_HEADER ? g_sel - 1 : g_sel;
    if (g_sel >= g_top + VISIBLE_ROWS) g_top = g_sel - VISIBLE_ROWS + 1;
    snprintf(g_menu.title, sizeof g_menu.title, "Lighting%s", any_set() ? "   (* = changed)" : "");
    for (i = g_top; i < g_nitems && n < VISIBLE_ROWS; i++, n++) {
        const LItem *it = &g_items[i];
        if (it->kind == K_HEADER)
            snprintf(g_menu.lines[n], sizeof g_menu.lines[0], "\xE2\x80\x94 %s \xE2\x80\x94", it->label);
        else if (!g_haveGame && !g_set[i])
            snprintf(g_menu.lines[n], sizeof g_menu.lines[0], "   %s\t-", it->label);
        else
            snprintf(g_menu.lines[n], sizeof g_menu.lines[0], "   %s\t%s%.2f", it->label, g_set[i] ? "* " : "",
                     cur_value(i));
    }
    g_menu.nlines = n;
    g_menu.selected = g_sel - g_top;
    snprintf(g_menu.footer, sizeof g_menu.footer,
             "\xE2\x86\x90\xE2\x86\x92 change (Shift x5, Ctrl fine)  Backspace reset  Del reset all  "
             "1-4 load  Shift+1-4 save  Esc close");
}

void light_frame(void)
{
    int i;
    if (!g_open) return;
    if (input_key_pressed(VK_ESCAPE)) {
        light_menu_set(0);
        return;
    }
    if (pressed_repeat(VK_UP)) move_sel(-1);
    if (pressed_repeat(VK_DOWN)) move_sel(1);
    if (pressed_repeat(VK_PRIOR)) for (i = 0; i < 6; i++) move_sel(-1);
    if (pressed_repeat(VK_NEXT)) for (i = 0; i < 6; i++) move_sel(1);
    {
        int dir = pressed_repeat(VK_RIGHT) ? 1 : pressed_repeat(VK_LEFT) ? -1 : 0;
        if (dir && (g_haveGame || g_set[g_sel])) {
            float step = g_items[g_sel].step;
            if (input_key_down(VK_SHIFT)) step *= 5.0f;
            if (input_key_down(VK_CONTROL)) step *= 0.2f;
            set_value(g_sel, cur_value(g_sel) + dir * step);
        }
    }
    if (input_key_pressed(VK_BACK)) g_set[g_sel] = 0;
    if (input_key_pressed(VK_DELETE)) {
        memset(g_set, 0, sizeof g_set);
        toast("Lighting: all changes reset");
    }
    for (i = 0; i < 4; i++) {
        char name[16];
        if (!input_key_pressed('1' + i)) continue;
        snprintf(name, sizeof name, "preset%d", i + 1);
        if (input_key_down(VK_SHIFT)) {
            toast("Lighting preset %d saved (%d values)", i + 1, preset_save(name));
        } else {
            int n = preset_load(name);
            toast(n < 0 ? "Lighting preset %d is empty" : "Lighting preset %d loaded", i + 1);
        }
    }
    fill_panel();
}

void light_shutdown(void)
{
    light_menu_set(0);
}

/* light                      list values
 * light <id> <value>         override, e.g. light sun_pitch 25
 * light reset [id]           one or all
 * light save|load <name>     presets (the panel uses preset1..4) */
int light_command(const char *cmd, const char *arg, char *out, int outsz)
{
    char a[32] = {0}, b[64] = {0};
    int i, len;
    if (_stricmp(cmd, "light")) return 0;
    if (!available()) return snprintf(out, outsz, "lighting not available in this game"), 1;
    sscanf(arg, "%31s %63s", a, b);
    if (!_stricmp(a, "reset")) {
        if (b[0] && (i = find_item(b)) >= 0) g_set[i] = 0;
        else memset(g_set, 0, sizeof g_set);
    } else if (!_stricmp(a, "save") && b[0]) {
        return snprintf(out, outsz, "saved %d values as %s", preset_save(b), b), 1;
    } else if (!_stricmp(a, "load") && b[0]) {
        return snprintf(out, outsz, "loaded %d values from %s", preset_load(b), b), 1;
    } else if (!_stricmp(a, "menu")) {
        light_menu_set(_stricmp(b, "off") != 0);
    } else if (a[0]) {
        if ((i = find_item(a)) < 0) return snprintf(out, outsz, "unknown item '%s'", a), 1;
        if (b[0]) set_value(i, (float)atof(b));
    }
    len = snprintf(out, outsz, "lighting (game values %s):", g_haveGame ? "known" : "not yet seen");
    for (i = 0; i < g_nitems && len < outsz - 60; i++)
        if (g_items[i].id)
            len += snprintf(out + len, outsz - len, "\n %-16s %s%.3f", g_items[i].id, g_set[i] ? "*" : " ", cur_value(i));
    return 1;
}
