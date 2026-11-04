/* Interface between the loader (dinput8.dll, owns all long-lived hooks) and the core
 * (_mod/drakensang_mod.dll, all features; reloadable while the game runs). */
#pragma once
#include "common.h"

#define MOD_HOST_VERSION 1

typedef struct ModHost {
    int version;
    const WCHAR *gameDir, *modDir;
    IDirect3DDevice9 **dev;          /* current device (updated by the loader) */
    HWND *hwnd;
    volatile int *blockKeyboard, *blockMouse;
    void (*take_mouse)(int *dx, int *dy, int *dz);
    void (*input_stats)(char *out, int outsz);
} ModHost;

typedef struct ModCore {
    void (*shutdown)(void);                    /* undo every patch made by the core */
    void (*device_created)(IDirect3DDevice9 *dev);
    void (*device_lost)(void);
    void (*device_reset)(void);
    void (*present)(IDirect3DDevice9 *dev);    /* once per frame, before the real Present */
} ModCore;

typedef int (*PFN_ModCoreInit)(const ModHost *host, ModCore *core);
