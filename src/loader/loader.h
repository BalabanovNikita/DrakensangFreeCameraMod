/* Loader (dinput8.dll): long-lived hooks and the reloadable core. */
#pragma once
#include "../host.h"

extern IDirect3DDevice9 *g_dev;
extern HWND g_hwnd;
extern volatile LONG g_presentCount;
extern volatile int g_blockKeyboard, g_blockMouse;

void **iat_slot(HMODULE mod, const char *dll, const char *func);
void  *iat_hook(HMODULE mod, const char *dll, const char *func, void *repl);

/* d3dhook.c */
void *d3d_hook_create(void *realFn);

/* dinput.c */
void input_wrap_dinput(void *di);
void input_take_mouse(int *dx, int *dy, int *dz);
void input_stats(char *out, int outsz);
void input_install_message_filter(HMODULE exe);

/* coreload.c */
extern ModCore g_core;
extern int g_coreLoaded;
void core_ensure_loaded(void);
void core_poll_reload(void);
