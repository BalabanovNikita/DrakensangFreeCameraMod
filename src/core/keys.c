/* The mod's own hotkeys: async key state, edge detection per tick. */
#include "core.h"

static int g_active;
static int g_watch[64], g_nwatch;
static BYTE g_prev[256], g_cur[256];
static BYTE g_synth[256];   /* presses injected by dev commands */

int input_game_active(void)
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg) return 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

int input_key_down(int vk)
{
    if (vk <= 0 || vk > 255) return 0;
    return g_active && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

int input_key_pressed(int vk)
{
    int i;
    if (vk <= 0 || vk > 255) return 0;
    if (g_synth[vk]) return 1;
    for (i = 0; i < g_nwatch; i++)
        if (g_watch[i] == vk) return g_cur[vk] && !g_prev[vk];
    if (g_nwatch < 64) {
        g_watch[g_nwatch++] = vk;
        g_cur[vk] = g_prev[vk] = (BYTE)input_key_down(vk);
    }
    return 0;
}

void input_synth_press(int vk)
{
    if (vk > 0 && vk < 256) g_synth[vk] = 1;   /* cleared by the next input_frame */
}

void input_frame(void)
{
    int i;
    g_active = input_game_active();
    memset(g_synth, 0, sizeof g_synth);
    for (i = 0; i < g_nwatch; i++) {
        int vk = g_watch[i];
        g_prev[vk] = g_cur[vk];
        g_cur[vk] = (BYTE)input_key_down(vk);
    }
}
