/* Level menu and travel.
 *
 * The list comes from <game>\drakensang_mod_levels.txt (tools/gen_levels.py, from the world DB).
 * Travel uses the game's own path for scripted transitions: LocationManager::ChangeLocation
 * (location, entry point, true), as executed by Actions::LoadLocation. */
#include "game.h"

typedef struct Level {
    char id[64];
    char name[96];
    char entries[2048];    /* comma separated, first = default */
    float arrival[4];      /* x y z yaw: move the party there after travelling via the default entry */
    int  hasArrival;
    int  nentries;
    int  entrySel;
} Level;

#define MAX_LEVELS 128
static Level g_levels[MAX_LEVELS];
static int g_nlevels;
static int g_open, g_sel, g_top;
static int g_extrasMode, g_xsel, g_xtop;   /* second page: cut / unused locations */
static int g_ok = -1;
static double g_repeatAt;
static int g_repeatVk;
static int g_travelQueued;   /* a deferred travel has not run yet */

#define VISIBLE_ROWS 16

typedef void (__fastcall *PFN_ChangeLocation)(void *self, void *edx, const void *loc, const void *entry, int flag);

static int available(void)
{
    if (g_ok < 0) {
        g_ok = game_ok();
        mlog("level travel: %s", g_ok ? "available" : "DISABLED (signature mismatch)");
    }
    return g_ok;
}

void levels_load(void)
{
    WCHAR path[MAX_PATH];
    HANDLE f;
    static char buf[256 * 1024];
    DWORD n = 0;
    char *line, *save = NULL;

    g_nlevels = 0;
    /* special row (id starting with '@'): opens the list of cut / unused locations */
    memset(&g_levels[0], 0, sizeof g_levels[0]);
    strcpy(g_levels[0].id, "@extras");
    snprintf(g_levels[0].name, sizeof g_levels[0].name,
             "\xE2\x98\x85 Cut & unused locations (%d)  \xE2\x86\x92", extras_count());
    strcpy(g_levels[0].entries, "-");
    g_levels[0].nentries = 1;
    g_nlevels = 1;
    /* composite levels (several levels assembled into one), id "@c<index>" */
    {
        int i;
        for (i = 0; i < composite_count() && g_nlevels < MAX_LEVELS; i++) {
            Level *lv = &g_levels[g_nlevels++];
            memset(lv, 0, sizeof *lv);
            snprintf(lv->id, sizeof lv->id, "@c%d", i);
            snprintf(lv->name, sizeof lv->name, "\xE2\x97\x86 %s", composite_title(i));
            strcpy(lv->entries, "-");
            lv->nentries = 1;
        }
    }
    game_data_file(path, L"drakensang_mod_levels");
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        mlog("levels: %ls not found", path);
        return;
    }
    ReadFile(f, buf, sizeof buf - 1, &n, NULL);
    CloseHandle(f);
    buf[n] = 0;
    for (line = strtok_s(buf, "\r\n", &save); line && g_nlevels < MAX_LEVELS; line = strtok_s(NULL, "\r\n", &save)) {
        Level *lv = &g_levels[g_nlevels];
        char *p1, *p2, *p3, *c;
        if (line[0] == '#' || !(p1 = strchr(line, '|')) || !(p2 = strchr(p1 + 1, '|'))) continue;
        *p1 = *p2 = 0;
        memset(lv, 0, sizeof *lv);
        if ((p3 = strchr(p2 + 1, '|'))) {
            *p3 = 0;
            lv->hasArrival = sscanf(p3 + 1, "%f %f %f %f", &lv->arrival[0], &lv->arrival[1], &lv->arrival[2],
                                    &lv->arrival[3]) >= 3;
        }
        strncpy(lv->id, line, sizeof lv->id - 1);
        strncpy(lv->name, p1 + 1, sizeof lv->name - 1);
        strncpy(lv->entries, p2 + 1, sizeof lv->entries - 1);
        lv->nentries = lv->entries[0] ? 1 : 0;
        for (c = lv->entries; *c; c++)
            if (*c == ',') lv->nentries++;
        if (lv->nentries) g_nlevels++;
    }
    mlog("levels: %d loaded from %ls", g_nlevels, path);
}

static const char *entry_at(const Level *lv, int idx, char *out, int outsz)
{
    const char *s = lv->entries;
    int i, n = 0;
    for (i = 0; i < idx && s; i++) {
        s = strchr(s, ',');
        if (s) s++;
    }
    if (!s) s = "";
    while (s[n] && s[n] != ',' && n < outsz - 1) {
        out[n] = s[n];
        n++;
    }
    out[n] = 0;
    return out;
}

static const Level *find_level(const char *id)
{
    int i;
    for (i = 0; i < g_nlevels; i++)
        if (!_stricmp(g_levels[i].id, id)) return &g_levels[i];
    return NULL;
}

/* Runs from the message pump (defer.c), outside the render frame. */
static void do_travel(const char *level, const char *entry)
{
    BYTE *mgr = *(BYTE **)ADDR_LOCATIONMGR_PTR;
    BYTE loc[NSTRING_SIZE], ent[NSTRING_SIZE];
    g_travelQueued = 0;
    if (!mgr || !game_readable(mgr, 0x100) || *(int *)(mgr + LOCMGR_PENDING_LEN) != 0) {
        mlog("travel: skipped (no manager or a change is pending)");
        return;
    }
    nstring_make(loc, level);
    nstring_make(ent, entry);
    ((PFN_ChangeLocation)ADDR_CHANGE_LOCATION)(mgr, NULL, loc, ent, 1);
    nstring_free(loc);
    nstring_free(ent);
    mlog("travel: %s @ %s", level, entry);
}

/* LevelLoader::Load: the level is built synchronously inside it while the loading screen keeps
 * pumping messages, so deferred work (which runs from the message pump) must wait for it. */
static Detour d_load;
static DWORD (__cdecl *orig_load)(DWORD a, DWORD b, DWORD c, DWORD d);
static volatile LONG g_loadDepth, g_loadSerial;

static DWORD __cdecl hk_load(DWORD a, DWORD b, DWORD c, DWORD d)
{
    DWORD r;
    g_loadDepth++;
    r = orig_load(a, b, c, d);
    g_loadDepth--;
    g_loadSerial++;
    return r;
}

void levels_init(void)
{
    if (game_ok() && G->level_load && !d_load.active)
        mlog("level load tracking: %s", detour_install(&d_load, G->level_load, G->pro_level_load, (void *)hk_load,
                                                       (void **)&orig_load) ? "on" : "FAILED");
}

int level_loading(void)
{
    return g_loadDepth > 0;
}

int level_load_serial(void)
{
    return g_loadSerial;
}

/* 1 while LocationManager has a location change pending (requested, not finished). */
int level_change_pending(void)
{
    BYTE *mgr;
    if (!game_ok()) return 0;
    mgr = *(BYTE **)ADDR_LOCATIONMGR_PTR;
    return mgr && game_readable(mgr, 0x100) && *(int *)(mgr + LOCMGR_PENDING_LEN) != 0;
}

/* Arrival point: once the travel has finished, move the party (levels without an entry where we want). */
static struct { int active, sawBusy, wait, serial; float p[4]; double since; } g_arrival;

static void arrival_arm(const char *level, const char *entry)
{
    const Level *lv = find_level(level);
    char def[160];
    g_arrival.active = 0;
    if (!lv || !lv->hasArrival || strcmp(entry_at(lv, 0, def, sizeof def), entry)) return;
    memcpy(g_arrival.p, lv->arrival, sizeof g_arrival.p);
    g_arrival.active = 1;
    g_arrival.sawBusy = g_arrival.wait = 0;
    g_arrival.serial = level_load_serial();
    g_arrival.since = now_seconds();
}

static void arrival_frame(void)
{
    if (!g_arrival.active) return;
    if (now_seconds() - g_arrival.since > 300.0) {
        g_arrival.active = 0;
        return;
    }
    if (level_travel_queued() || level_change_pending() || level_loading()) {
        g_arrival.sawBusy = 1;
        g_arrival.wait = 0;
        return;
    }
    if (!g_arrival.sawBusy || !cam_main()) return;
    if (++g_arrival.wait < 45) return;   /* let the entry placement and fade-in finish */
    g_arrival.active = 0;
    party_teleport(g_arrival.p[0], g_arrival.p[1], g_arrival.p[2], g_arrival.p[3]);
    mlog("travel: arrival point %.1f %.1f %.1f", g_arrival.p[0], g_arrival.p[1], g_arrival.p[2]);
}

/* Schedule a switch to `level` at `entry`. Returns 1 if accepted, otherwise writes a reason. */
int level_travel(const char *level, const char *entry, char *err, int errsz)
{
    BYTE *mgr;
    if (!available()) return snprintf(err, errsz, "level travel unavailable (unknown game build)"), 0;
    mgr = *(BYTE **)ADDR_LOCATIONMGR_PTR;
    if (!mgr || !game_readable(mgr, 0x100)) return snprintf(err, errsz, "no location manager (not in game?)"), 0;
    if (*(int *)(mgr + LOCMGR_PENDING_LEN) != 0 || g_travelQueued)
        return snprintf(err, errsz, "a level change is already in progress"), 0;
    if (!level[0] || !entry[0]) return snprintf(err, errsz, "level or entry missing"), 0;
    if (freecam_active()) freecam_set(0);
    if (!defer_call(do_travel, level, entry)) return snprintf(err, errsz, "cannot queue the request"), 0;
    g_travelQueued = 1;
    arrival_arm(level, entry);
    return 1;
}

int level_travel_queued(void)
{
    return g_travelQueued;
}

int levels_menu_open(void)
{
    return g_open;
}

void levels_menu_set(int open)
{
    if (!open) g_extrasMode = 0;
    if (open && !g_nlevels) levels_load();
    if (open && !g_nlevels) {
        toast("Level list missing: run _mod/tools/gen_levels.py");
        return;
    }
    g_open = open;
    g_menu.visible = open;
}

/* Edge-triggered press with auto-repeat while held. */
static int pressed_repeat(int vk)
{
    double now = now_seconds();
    if (input_key_pressed(vk)) {
        g_repeatVk = vk;
        g_repeatAt = now + 0.35;
        return 1;
    }
    if (g_repeatVk == vk && input_key_down(vk) && now >= g_repeatAt) {
        g_repeatAt = now + 0.045;
        return 1;
    }
    return 0;
}

static void fill_menu(void)
{
    Level *lv = &g_levels[g_sel];
    char entry[160];
    int i;
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + VISIBLE_ROWS) g_top = g_sel - VISIBLE_ROWS + 1;
    snprintf(g_menu.title, sizeof g_menu.title, "Travel to level  (%d/%d)", g_sel + 1, g_nlevels);
    g_menu.nlines = 0;
    for (i = g_top; i < g_nlevels && g_menu.nlines < VISIBLE_ROWS; i++)
        if (g_levels[i].id[0] == '@')
            snprintf(g_menu.lines[g_menu.nlines++], sizeof g_menu.lines[0], "%s", g_levels[i].name);
        else
            snprintf(g_menu.lines[g_menu.nlines++], sizeof g_menu.lines[0], "%s   [%s]", g_levels[i].name,
                     g_levels[i].id);
    g_menu.selected = g_sel - g_top;
    if (lv->id[0] == '@') {
        snprintf(g_menu.footer, sizeof g_menu.footer, "Enter: open the list     Esc: close");
        return;
    }
    snprintf(g_menu.footer, sizeof g_menu.footer,
             "Entry %d/%d: %s     \xE2\x86\x91\xE2\x86\x93 level  \xE2\x86\x90\xE2\x86\x92 entry  Enter travel  Esc close",
             lv->entrySel + 1, lv->nentries, entry_at(lv, lv->entrySel, entry, sizeof entry));
}

/* Rows of the extras page: the locations, then "remove", then "back".
 * handleKeys = 0 on the frame the page is opened, so the Enter that opened it is not reused. */
static void extras_page(int handleKeys)
{
    int n = extras_count(), rows = n + 2, i;
    if (!handleKeys) goto draw;
    if (pressed_repeat(VK_UP)) g_xsel = (g_xsel + rows - 1) % rows;
    if (pressed_repeat(VK_DOWN)) g_xsel = (g_xsel + 1) % rows;
    if (pressed_repeat(VK_PRIOR)) g_xsel = g_xsel > VISIBLE_ROWS ? g_xsel - VISIBLE_ROWS : 0;
    if (pressed_repeat(VK_NEXT)) g_xsel = g_xsel + VISIBLE_ROWS < rows ? g_xsel + VISIBLE_ROWS : rows - 1;
    if (input_key_pressed(VK_HOME)) g_xsel = 0;
    if (input_key_pressed(VK_END)) g_xsel = rows - 1;
    if (input_key_pressed(VK_LEFT) || input_key_pressed(VK_BACK) || (input_key_pressed(VK_RETURN) && g_xsel == n + 1)) {
        g_extrasMode = 0;
        return;
    }
    if (input_key_pressed(VK_RETURN)) {
        char msg[160];
        levels_menu_set(0);
        if (g_xsel == n) {
            extras_reset_anchor();
            toast(spawn_clear() ? "Placed model removed" : "Nothing to remove");
        } else if (extras_spawn(g_xsel, msg, sizeof msg)) {
            toast("%s  (free camera; F7 \xE2\x86\x92 \xE2\x98\x85 to remove)", msg);
        } else {
            toast("Cannot place: %s", msg);
        }
        return;
    }
draw:
    if (g_xsel < g_xtop) g_xtop = g_xsel;
    if (g_xsel >= g_xtop + VISIBLE_ROWS) g_xtop = g_xsel - VISIBLE_ROWS + 1;
    snprintf(g_menu.title, sizeof g_menu.title, "Cut & unused locations  (%d/%d)", g_xsel + 1, rows);
    g_menu.nlines = 0;
    for (i = g_xtop; i < rows && g_menu.nlines < VISIBLE_ROWS; i++) {
        const char *t = i < n ? extras_title(i) : i == n ? "\xE2\x9C\x96 Remove placed model" : "\xE2\x86\x90 Back to levels";
        snprintf(g_menu.lines[g_menu.nlines++], sizeof g_menu.lines[0], "%s", t);
    }
    g_menu.selected = g_xsel - g_xtop;
    snprintf(g_menu.footer, sizeof g_menu.footer,
             "Enter: place below this level and fly there     \xE2\x86\x90 back     Esc close");
}

void levels_frame(void)
{
    Level *lv;
    arrival_frame();
    if (!g_open) return;
    if (input_key_pressed(VK_ESCAPE)) {
        levels_menu_set(0);
        return;
    }
    if (g_extrasMode) {
        extras_page(1);
        return;
    }
    if (pressed_repeat(VK_UP)) g_sel = (g_sel + g_nlevels - 1) % g_nlevels;
    if (pressed_repeat(VK_DOWN)) g_sel = (g_sel + 1) % g_nlevels;
    if (pressed_repeat(VK_PRIOR)) g_sel = g_sel > VISIBLE_ROWS ? g_sel - VISIBLE_ROWS : 0;
    if (pressed_repeat(VK_NEXT)) g_sel = g_sel + VISIBLE_ROWS < g_nlevels ? g_sel + VISIBLE_ROWS : g_nlevels - 1;
    if (input_key_pressed(VK_HOME)) g_sel = 0;
    if (input_key_pressed(VK_END)) g_sel = g_nlevels - 1;
    lv = &g_levels[g_sel];
    if (pressed_repeat(VK_LEFT)) lv->entrySel = (lv->entrySel + lv->nentries - 1) % lv->nentries;
    if (pressed_repeat(VK_RIGHT)) lv->entrySel = (lv->entrySel + 1) % lv->nentries;
    if (input_key_pressed(VK_RETURN)) {
        char entry[160], err[160];
        if (!strncmp(lv->id, "@c", 2)) {   /* composite level */
            levels_menu_set(0);
            if (composite_start(atoi(lv->id + 2), err, sizeof err))
                toast("Travelling to %s", composite_title(atoi(lv->id + 2)));
            else
                toast("Cannot travel: %s", err);
            return;
        }
        if (lv->id[0] == '@') {   /* "@extras": open the second page */
            g_extrasMode = 1;
            extras_page(0);
            return;
        }
        entry_at(lv, lv->entrySel, entry, sizeof entry);
        levels_menu_set(0);
        if (level_travel(lv->id, entry, err, sizeof err))
            toast("Travelling to %s", lv->name);
        else
            toast("Cannot travel: %s", err);
        return;
    }
    fill_menu();
}

int levels_command(const char *cmd, const char *arg, char *out, int outsz)
{
    if (!_stricmp(cmd, "travel")) {
        char level[64] = {0}, entry[160] = {0}, err[160];
        const Level *lv;
        sscanf(arg, "%63s %159s", level, entry);
        if (!g_nlevels) levels_load();
        lv = find_level(level);
        if (!entry[0] || (entry[0] == '#' && entry[1])) {
            int idx = entry[0] == '#' ? atoi(entry + 1) - 1 : 0;
            if (!lv) return snprintf(out, outsz, "unknown level '%s'", level), 1;
            entry_at(lv, idx < 0 ? 0 : idx, entry, sizeof entry);
        }
        if (level_travel(level, entry, err, sizeof err))
            snprintf(out, outsz, "travelling to %s @ %s", level, entry);
        else
            snprintf(out, outsz, "FAILED: %s", err);
        return 1;
    }
    if (!_stricmp(cmd, "levels")) {
        int i, len;
        if (!g_nlevels) levels_load();
        len = snprintf(out, outsz, "%d levels:", g_nlevels);
        for (i = 0; i < g_nlevels && len < outsz - 80; i++)
            len += snprintf(out + len, outsz - len, " %s(%d)", g_levels[i].id, g_levels[i].nentries);
        return 1;
    }
    if (!_stricmp(cmd, "menu")) {
        levels_menu_set(_stricmp(arg, "off") != 0);
        if (!_stricmp(arg, "extras")) g_extrasMode = 1;
        snprintf(out, outsz, "menu=%d", g_open);
        return 1;
    }
    return 0;
}
