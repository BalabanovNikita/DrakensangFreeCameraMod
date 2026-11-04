/* drakensang_mod.ini in the game root. Missing keys fall back to defaults. */
#include "../common.h"

Config g_cfg;

static const struct { const char *name; int vk; } g_keys[] = {
    {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4}, {"F5", VK_F5}, {"F6", VK_F6},
    {"F7", VK_F7}, {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
    {"Space", VK_SPACE}, {"Enter", VK_RETURN}, {"Escape", VK_ESCAPE}, {"Tab", VK_TAB},
    {"Backspace", VK_BACK}, {"Insert", VK_INSERT}, {"Delete", VK_DELETE}, {"Home", VK_HOME},
    {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT}, {"Up", VK_UP}, {"Down", VK_DOWN},
    {"Left", VK_LEFT}, {"Right", VK_RIGHT}, {"Shift", VK_SHIFT}, {"Ctrl", VK_CONTROL},
    {"Alt", VK_MENU}, {"CapsLock", VK_CAPITAL}, {"Pause", VK_PAUSE}, {"ScrollLock", VK_SCROLL},
    {"PrintScreen", VK_SNAPSHOT}, {"Tilde", VK_OEM_3}, {"Numpad0", VK_NUMPAD0},
    {"Numpad1", VK_NUMPAD1}, {"Numpad2", VK_NUMPAD2}, {"Numpad3", VK_NUMPAD3},
    {"Numpad4", VK_NUMPAD4}, {"Numpad5", VK_NUMPAD5}, {"Numpad6", VK_NUMPAD6},
    {"Numpad7", VK_NUMPAD7}, {"Numpad8", VK_NUMPAD8}, {"Numpad9", VK_NUMPAD9},
    {"NumpadPlus", VK_ADD}, {"NumpadMinus", VK_SUBTRACT}, {"NumpadMul", VK_MULTIPLY},
    {"NumpadDiv", VK_DIVIDE}, {"MouseMiddle", VK_MBUTTON}, {"MouseX1", VK_XBUTTON1},
    {"MouseX2", VK_XBUTTON2}, {"None", 0},
};

const char *vk_name(int vk)
{
    static char one[2];
    int i;
    for (i = 0; i < (int)(sizeof g_keys / sizeof g_keys[0]); i++)
        if (g_keys[i].vk == vk) return g_keys[i].name;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        one[0] = (char)vk;
        one[1] = 0;
        return one;
    }
    return "?";
}

static int parse_key(const char *s, int def)
{
    int i;
    while (*s == ' ') s++;
    if (!*s) return def;
    if (s[1] == 0 || s[1] == ' ') {
        char c = s[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return (int)strtol(s, NULL, 16);
    for (i = 0; i < (int)(sizeof g_keys / sizeof g_keys[0]); i++)
        if (_stricmp(g_keys[i].name, s) == 0) return g_keys[i].vk;
    return def;
}

int parse_key_name(const char *s)
{
    return parse_key(s, 0);
}

static WCHAR g_ini[MAX_PATH];

static void get_str(const char *sec, const char *key, char *out, int outsz)
{
    WCHAR wsec[64], wkey[64], wval[512];
    MultiByteToWideChar(CP_ACP, 0, sec, -1, wsec, 64);
    MultiByteToWideChar(CP_ACP, 0, key, -1, wkey, 64);
    GetPrivateProfileStringW(wsec, wkey, L"", wval, 512, g_ini);
    WideCharToMultiByte(CP_ACP, 0, wval, -1, out, outsz, NULL, NULL);
    /* strip trailing comment / spaces */
    {
        char *p = strchr(out, ';');
        if (p) *p = 0;
        p = out + strlen(out);
        while (p > out && (p[-1] == ' ' || p[-1] == '\t')) *--p = 0;
    }
}

static int get_key(const char *sec, const char *key, int def)
{
    char v[64];
    get_str(sec, key, v, sizeof v);
    return parse_key(v, def);
}

static float get_float(const char *sec, const char *key, float def)
{
    char v[64];
    get_str(sec, key, v, sizeof v);
    return v[0] ? (float)atof(v) : def;
}

static int get_int(const char *sec, const char *key, int def)
{
    char v[64];
    get_str(sec, key, v, sizeof v);
    return v[0] ? atoi(v) : def;
}

void config_load(void)
{
    Config *c = &g_cfg;
    wcscpy(g_ini, g_gameDir);
    wcscat(g_ini, L"drakensang_mod.ini");

    c->vk_ui         = get_key("hotkeys", "toggle_ui", VK_F10);
    c->vk_freecam    = get_key("hotkeys", "toggle_freecam", VK_F11);
    c->vk_levels     = get_key("hotkeys", "level_menu", VK_F7);
    c->vk_freeze     = get_key("hotkeys", "freeze_time", VK_F6);
    c->vk_party      = get_key("hotkeys", "party_to_camera", VK_F4);
    c->vk_screenshot = get_key("hotkeys", "screenshot", VK_F3);
    c->vk_fog        = get_key("hotkeys", "toggle_fog", VK_F2);
    c->vk_light      = get_key("hotkeys", "lighting_menu", VK_F1);

    c->vk_fwd        = get_key("freecam", "forward", 'W');
    c->vk_back       = get_key("freecam", "back", 'S');
    c->vk_left       = get_key("freecam", "left", 'A');
    c->vk_right      = get_key("freecam", "right", 'D');
    c->vk_up         = get_key("freecam", "up", VK_SPACE);
    c->vk_down       = get_key("freecam", "down", 'C');
    c->vk_roll_left  = get_key("freecam", "roll_left", 'Q');
    c->vk_roll_right = get_key("freecam", "roll_right", 'E');
    c->vk_fov_in     = get_key("freecam", "fov_in", 'Z');
    c->vk_fov_out    = get_key("freecam", "fov_out", 'X');
    c->vk_reset      = get_key("freecam", "reset_roll_fov", VK_BACK);
    c->vk_fast       = get_key("freecam", "fast", VK_SHIFT);
    c->vk_slow       = get_key("freecam", "slow", VK_MENU);
    c->cam_speed     = get_float("freecam", "speed", 6.0f);
    c->cam_fast_mult = get_float("freecam", "fast_multiplier", 4.0f);
    c->cam_slow_mult = get_float("freecam", "slow_multiplier", 0.25f);
    c->mouse_sens    = get_float("freecam", "mouse_sensitivity", 0.12f);
    c->invert_y      = get_int("freecam", "invert_y", 0);
    c->roll_speed    = get_float("freecam", "roll_speed", 45.0f);
    c->fov_speed     = get_float("freecam", "fov_speed", 20.0f);

    c->toasts             = get_int("overlay", "messages", 1);
    c->toasts_when_hidden = get_int("overlay", "messages_when_ui_hidden", 1);
    c->font_scale         = get_float("overlay", "font_scale", 1.0f);

    c->far_objects = get_int("graphics", "unlimited_draw_distance", 1);
    c->far_lights  = get_int("graphics", "unlimited_light_distance", 1);
    c->ignore_zones = get_int("graphics", "ignore_visibility_zones", 1);
    c->fog_scale    = get_float("graphics", "fog_distance", 1.0f);
    c->far_models   = get_int("graphics", "unlimited_model_distance", 1);
    c->lod_detail   = get_float("graphics", "model_detail_distance", 2.0f);
    c->far_clip     = get_float("graphics", "far_clip", 10000.0f);
    c->tree_detail  = get_float("graphics", "tree_detail_distance", 3.0f);
    c->borderless  = get_int("display", "borderless", 1);

    c->dev_log     = get_int("dev", "log", 1);
    c->dev_dump    = get_int("dev", "dump_image", 0);
    c->dev_cmdfile = get_int("dev", "command_file", 0);
    get_str("dev", "extra_args", c->extra_args, sizeof c->extra_args);
}
