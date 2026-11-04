/* Core (drakensang_mod.dll): all features; loaded and reloaded by the loader. */
#pragma once
#include "../host.h"

extern const ModHost *g_host;
#define g_dev           (*g_host->dev)
#define g_hwnd          (*g_host->hwnd)
#define g_blockKeyboard (*g_host->blockKeyboard)
#define g_blockMouse    (*g_host->blockMouse)

/* keys.c: the mod's own hotkeys (async key state, only while the game window is active) */
int  input_key_down(int vk);
int  input_key_pressed(int vk);     /* edge-triggered, valid during mod_tick */
void input_frame(void);             /* once per tick */
int  input_game_active(void);
void input_synth_press(int vk);     /* dev commands: act as if the hotkey was pressed */

/* screenshot.c */
void shot_request(const WCHAR *path);   /* NULL = auto name in <game>\screenshots */
void shot_request_overlay(const WCHAR *path);   /* dev: include the mod overlay */
void shot_take_if_pending(IDirect3DDevice9 *dev, int afterOverlay);

/* overlay.c */
void overlay_device_created(IDirect3DDevice9 *dev);
void overlay_device_lost(void);
void overlay_device_reset(void);
void overlay_shutdown(void);
void overlay_draw(IDirect3DDevice9 *dev);
void toast(const char *fmt, ...);
typedef struct OverlayMenu {
    int visible;
    char title[128];
    char lines[32][160];
    int  nlines;
    int  selected;          /* index into lines, -1 = none */
    char footer[200];
} OverlayMenu;
extern OverlayMenu g_menu;

/* mod.c */
extern int g_uiHidden;
void mod_init(void);
void mod_shutdown(void);
void mod_tick(void);
int  mod_command(const char *line, char *out, int outsz);
int  dump_image(void);

/* devcmd.c */
void devcmd_poll(void);

/* ui_toggle.c */
int  ui_set_hidden(int hidden);
void ui_frame(void);
int  ui_list_passes(char *out, int outsz);

/* freecam.c */
BYTE *cam_main(void);
int  freecam_active(void);
int  freecam_set(int on);
void freecam_frame(void);
void freecam_shutdown(void);
int  freecam_position(float out[3]);
int  freecam_command(const char *cmd, const char *arg, char *out, int outsz);

/* levels.c */
void levels_load(void);
int  levels_menu_open(void);
void levels_menu_set(int open);
void levels_frame(void);
int  level_travel(const char *level, const char *entry, char *err, int errsz);
int  level_change_pending(void);
void levels_init(void);
int  level_loading(void);
int  level_load_serial(void);
int  level_travel_queued(void);
int  levels_command(const char *cmd, const char *arg, char *out, int outsz);

/* drawdist.c */
extern int g_farObjects, g_farLights, g_ignoreZones, g_farModels;
extern float g_lodDetail, g_farClip;
void drawdist_init(void);
void drawdist_frame(void);
void drawdist_shutdown(void);
int  fog_toggle(void);   /* returns new state, -1 if unavailable */
int  drawdist_command(const char *cmd, const char *arg, char *out, int outsz);

/* spawn.c */
int  spawn_model(const char *res, const float m[16]);
int  spawn_tree(const char *file, const float m[16], float nearLod, float farLod, const float bright[4]);
int  spawn_pending(void);
void spawn_set_hide(const char (*patterns)[48], int n);
int  spawn_hidden_count(void);
int  spawn_hide_attached(const char (*patterns)[48], int n);
int  spawn_list_entities(const char *pattern, char *out, int outsz);
int  spawn_clear(void);
int  spawn_count(void);
int  spawn_info(char *out, int outsz);
int  level_bounds(float out[6]);
BYTE *spawn_current_level(void);
void spawn_frame(void);
void spawn_shutdown(void);
/* freecam.c */
void freecam_goto(float x, float y, float z, float yawDeg, float pitchDeg);

/* light.c */
void light_init(void);
int  light_menu_open(void);
void light_menu_set(int open);
void light_frame(void);
void light_shutdown(void);
int  light_command(const char *cmd, const char *arg, char *out, int outsz);

/* party.c */
int  party_teleport(float x, float y, float z, float yawDeg);
int  party_to_camera(char *msg, int msgsz);
int  party_command(const char *cmd, const char *arg, char *out, int outsz);
float freecam_yaw(void);

/* composite.c */
int  composite_count(void);
const char *composite_title(int i);
int  composite_start(int i, char *err, int errsz);
void composite_frame(void);
int  composite_command(const char *cmd, const char *arg, char *out, int outsz);

/* extras.c */
void extras_load(void);
int  extras_count(void);
const char *extras_title(int i);
int  extras_spawn(int i, char *msg, int msgsz);
int  extras_temple(char *msg, int msgsz);
void extras_reset_anchor(void);
int  extras_command(const char *cmd, const char *arg, char *out, int outsz);
