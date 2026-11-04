/* Per-frame logic, hotkeys and the text command interface. */
#include "game.h"

int g_uiHidden;
static unsigned g_frame;

/* <mod>\dump\<exe name>_mem.bin */
static void dump_path(WCHAR *path)
{
    WCHAR exe[MAX_PATH], *name, *dot;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    if ((dot = wcsrchr(name, L'.'))) *dot = 0;
    wcscpy(path, g_modDir);
    wcscat(path, L"dump\\");
    CreateDirectoryW(path, NULL);
    wcscat(path, name);
    wcscat(path, L"_mem.bin");
}

/* Write the decrypted in-memory image of the game exe for offline analysis. */
int dump_image(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage, off = 0, written;
    WCHAR path[MAX_PATH];
    HANDLE f;
    static BYTE zero[4096];

    dump_path(path);
    f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        mlog("dump: cannot create file");
        return 0;
    }
    while (off < size) {
        MEMORY_BASIC_INFORMATION mbi;
        DWORD chunk;
        int readable;
        if (!VirtualQuery(base + off, &mbi, sizeof mbi)) break;
        chunk = (DWORD)((BYTE *)mbi.BaseAddress + mbi.RegionSize - (base + off));
        if (off + chunk > size) chunk = size - off;
        readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
        if (readable) {
            WriteFile(f, base + off, chunk, &written, NULL);
        } else {
            DWORD left = chunk;
            while (left) {
                DWORD n = left > sizeof zero ? sizeof zero : left;
                WriteFile(f, zero, n, &written, NULL);
                left -= n;
            }
        }
        off += chunk;
    }
    CloseHandle(f);
    mlog("dump: wrote %lu bytes of image at %p", (unsigned long)off, base);
    return 1;
}

void mod_init(void)
{
    WCHAR path[MAX_PATH];
    dump_path(path);
    if (g_cfg.dev_dump && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) dump_image();
    defer_init();
    if (!game_ok()) {
        mlog("game-specific features disabled for this exe");
        return;
    }
    levels_init();
    levels_load();
    drawdist_init();
    light_init();
}

void mod_shutdown(void)
{
    levels_menu_set(0);
    light_shutdown();
    drawdist_shutdown();
    spawn_shutdown();
    freecam_shutdown();
    ui_set_hidden(0);
    g_blockKeyboard = g_blockMouse = 0;
    detour_remove_all();
    defer_shutdown();
}

static void toggle_freecam(void)
{
    int on = !freecam_active();
    if (!freecam_set(on))
        toast("Free camera unavailable here");
    else if (on)
        toast("Free camera ON: mouse, WASD, %s/%s up/down, %s fast, wheel speed", vk_name(g_cfg.vk_up),
              vk_name(g_cfg.vk_down), vk_name(g_cfg.vk_fast));
    else
        toast("Free camera OFF");
}

static void toggle_ui(void)
{
    if (ui_set_hidden(!g_uiHidden))
        toast(g_uiHidden ? "UI hidden (%s)" : "UI visible (%s)", vk_name(g_cfg.vk_ui));
    else
        toast("UI toggle unavailable");
}

static void hotkeys(void)
{
    if (input_key_pressed(g_cfg.vk_screenshot)) shot_request(NULL);
    if (!game_ok()) {
        if (input_key_pressed(g_cfg.vk_ui) || input_key_pressed(g_cfg.vk_freecam) ||
            input_key_pressed(g_cfg.vk_levels) || input_key_pressed(g_cfg.vk_fog) || input_key_pressed(g_cfg.vk_light))
            toast("This game is not supported by the mod yet (only screenshots)");
        return;
    }
    if (input_key_pressed(g_cfg.vk_ui)) toggle_ui();
    if (input_key_pressed(g_cfg.vk_freecam)) toggle_freecam();
    if (input_key_pressed(g_cfg.vk_levels)) {
        light_menu_set(0);
        levels_menu_set(!levels_menu_open());
    }
    if (input_key_pressed(g_cfg.vk_light)) {
        levels_menu_set(0);
        light_menu_set(!light_menu_open());
    }
    if (input_key_pressed(g_cfg.vk_fog)) {
        int st = fog_toggle();
        toast(st < 0 ? "Fog control unavailable" : st ? "Fog ON" : "Fog OFF");
    }
    if (input_key_pressed(g_cfg.vk_freeze)) toast("Freeze time: not implemented yet");
    if (input_key_pressed(g_cfg.vk_party)) {
        char msg[128];
        if (!freecam_active()) toast("Party to camera: turn on the free camera (%s) and fly to the place first",
                                     vk_name(g_cfg.vk_freecam));
        else if (party_to_camera(msg, sizeof msg)) toast("Party: moving to the camera");
        else toast("Party to camera: %s", msg);
    }
}

void mod_tick(void)
{
    g_frame++;
    input_frame();
    if (g_cfg.dev_cmdfile) devcmd_poll();
    hotkeys();
    levels_frame();
    light_frame();
    freecam_frame();
    drawdist_frame();
    composite_frame();
    spawn_frame();
    ui_frame();
    g_blockKeyboard = g_blockMouse = freecam_active() || levels_menu_open() || light_menu_open();
}

/* ---- commands (dev command file) --------------------------------------- */
static int on_off(const char *arg, int current)
{
    if (!_stricmp(arg, "on") || !strcmp(arg, "1")) return 1;
    if (!_stricmp(arg, "off") || !strcmp(arg, "0")) return 0;
    return !current;   /* "toggle" or empty */
}

int mod_command(const char *line, char *out, int outsz)
{
    char cmd[32] = {0}, arg[256] = {0};
    sscanf(line, "%31s %255[^\r\n]", cmd, arg);
    out[0] = 0;

    if (!_stricmp(cmd, "ping")) {
        snprintf(out, outsz, "pong frame=%u t=%.2f", g_frame, now_seconds());
    } else if (!_stricmp(cmd, "status")) {
        snprintf(out, outsz, "frame=%u ui_hidden=%d active=%d dev=%p hwnd=%p block kb=%d mouse=%d", g_frame,
                 g_uiHidden, input_game_active(), (void *)g_dev, (void *)g_hwnd, g_blockKeyboard, g_blockMouse);
    } else if (!_stricmp(cmd, "input")) {
        g_host->input_stats(out, outsz);
    } else if (!_stricmp(cmd, "screenshot")) {
        WCHAR path[MAX_PATH], name[128];
        char nm[100] = {0}, opt[16] = {0};
        sscanf(arg, "%99s %15s", nm, opt);
        if (nm[0]) {
            MultiByteToWideChar(CP_UTF8, 0, nm, -1, name, 128);
            wcscpy(path, g_modDir);
            wcscat(path, L"shots\\");
            CreateDirectoryW(path, NULL);
            wcscat(path, name);
            wcscat(path, L".png");
            if (!_stricmp(opt, "overlay")) shot_request_overlay(path);
            else shot_request(path);
        } else {
            shot_request(NULL);
        }
        snprintf(out, outsz, "screenshot queued");
    } else if (!_stricmp(cmd, "toast")) {
        toast("%s", arg);
        snprintf(out, outsz, "ok");
    } else if (!_stricmp(cmd, "key")) {
        int vk = parse_key_name(arg);
        if (vk) {
            input_synth_press(vk);
            snprintf(out, outsz, "pressed %s", vk_name(vk));
        } else {
            snprintf(out, outsz, "unknown key '%s'", arg);
        }
    } else if (!_stricmp(cmd, "ui")) {
        int ok = ui_set_hidden(!on_off(arg, !g_uiHidden));
        snprintf(out, outsz, "%s ui_hidden=%d", ok ? "ok" : "FAILED", g_uiHidden);
    } else if (!_stricmp(cmd, "freecam")) {
        int ok = freecam_set(on_off(arg, freecam_active()));
        snprintf(out, outsz, "%s freecam=%d", ok ? "ok" : "FAILED", freecam_active());
    } else if (freecam_command(cmd, arg, out, outsz) || levels_command(cmd, arg, out, outsz) ||
               drawdist_command(cmd, arg, out, outsz) || extras_command(cmd, arg, out, outsz) ||
               light_command(cmd, arg, out, outsz) || composite_command(cmd, arg, out, outsz) ||
               party_command(cmd, arg, out, outsz)) {
        /* handled */
    } else if (!_stricmp(cmd, "defer")) {
        defer_stats(out, outsz);
    } else if (!_stricmp(cmd, "passes")) {
        ui_list_passes(out, outsz);
    } else if (!_stricmp(cmd, "block")) {
        int v = on_off(arg, g_blockKeyboard);
        g_blockKeyboard = g_blockMouse = v;
        snprintf(out, outsz, "input blocked=%d", v);
    } else if (!_stricmp(cmd, "peek")) {   /* peek <hex addr | *hex ptr> [+hexoff] [n]: floats and dwords */
        char a[32] = {0};
        unsigned long off = 0;
        int n = 16, i, len;
        BYTE *p;
        sscanf(arg, "%31s %lx %d", a, &off, &n);
        p = (BYTE *)strtoul(a[0] == '*' ? a + 1 : a, NULL, 16);
        if (a[0] == '*') p = game_readable(p, 4) ? *(BYTE **)p : NULL;
        p += off;
        if (n < 1 || n > 256) n = 16;
        if (!game_readable(p, n * 4)) return snprintf(out, outsz, "unreadable %p", p), 1;
        len = snprintf(out, outsz, "%p:", p);
        for (i = 0; i < n && len < outsz - 40; i++)
            len += snprintf(out + len, outsz - len, "%s+%03x %08lx %g", i % 4 ? "  " : "\n", (unsigned)(off + i * 4),
                            ((unsigned long *)p)[i], ((float *)p)[i]);
    } else if (!_stricmp(cmd, "dump")) {
        snprintf(out, outsz, dump_image() ? "dumped" : "dump failed");
    } else if (!_stricmp(cmd, "help")) {
        snprintf(out, outsz,
                 "commands: ping status input screenshot [name] toast <text> key <name> ui [on|off] passes "
                 "block [on|off] dump freecam [on|off] cam [get|set|move|turn] fov roll speed levels menu [on|off] "
                 "travel <level> [entry|#n] lod lodlights zones [on|off|toggle] fog [on|off|<scale>] temple extras extra <n> spawn <res> [x y z] despawn bounds lodmodels [on|off] loddetail <x> farclip <m> light [<id> <v>|reset|save|load|menu] peek");
    } else {
        snprintf(out, outsz, "unknown command '%s'", cmd);
        return 0;
    }
    return 1;
}
