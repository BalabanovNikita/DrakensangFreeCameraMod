/* Drakensang mod: declarations shared by the loader (dinput8.dll) and the core (drakensang_mod.dll). */
#pragma once

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <d3d9.h>
#include <d3dx9core.h>
#include <d3dx9tex.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define MOD_VERSION "0.2"

/* ---- shared/config.c ----------------------------------------------------- */
typedef struct Config {
    /* hotkeys (virtual-key codes) */
    int vk_ui, vk_freecam, vk_levels, vk_freeze, vk_party, vk_screenshot, vk_fog, vk_light;
    /* free camera */
    int vk_fwd, vk_back, vk_left, vk_right, vk_up, vk_down;
    int vk_roll_left, vk_roll_right, vk_fov_in, vk_fov_out, vk_reset;
    int vk_fast, vk_slow;
    float cam_speed;        /* metres per second */
    float cam_fast_mult, cam_slow_mult;
    float mouse_sens;       /* degrees per mouse count */
    int   invert_y;
    float roll_speed;       /* degrees per second */
    float fov_speed;        /* degrees per second */
    /* overlay */
    int   toasts;           /* show short status messages */
    int   toasts_when_hidden;
    float font_scale;
    /* graphics */
    int   far_objects;      /* no distance / small-size culling of objects and characters */
    int   far_lights;       /* same for light sources */
    int   ignore_zones;     /* render everything regardless of visibility boxes */
    float fog_scale;        /* fog distance multiplier, 0 = no fog */
    int   far_models;       /* models keep their lowest LOD at any distance instead of vanishing */
    float lod_detail;       /* model LOD switch distances multiplier (1 = game, 3 = full detail 3x farther) */
    float far_clip;         /* camera far clip plane in metres, 0 = game default */
    float tree_detail;      /* SpeedTree LOD distances multiplier, applied when a level loads */
    /* display */
    int   borderless;       /* replace exclusive fullscreen by a borderless window */
    /* dev */
    int   dev_log, dev_dump, dev_cmdfile;
    char  extra_args[256];
} Config;

extern Config g_cfg;
void config_load(void);
const char *vk_name(int vk);
int parse_key_name(const char *s);

/* ---- paths (set by the loader, copied by the core) ------------------------ */
extern WCHAR g_gameDir[MAX_PATH];   /* game root, with trailing backslash */
extern WCHAR g_modDir[MAX_PATH];    /* <game>\_mod\ */

/* ---- shared/util.c -------------------------------------------------------- */
double now_seconds(void);
void   patch_ptr(void **where, void *value);

/* ---- shared/log.c --------------------------------------------------------- */
void log_open(int truncate);
void log_close(void);
void mlog(const char *fmt, ...);
