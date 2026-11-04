/* Deferred game actions.
 *
 * Present() runs inside nGfxServer2's frame (between BeginFrame and EndFrame). Game calls that
 * render on their own - e.g. LocationManager::ChangeLocation draws a still frame for the loading
 * screen - assert "!this->inBeginFrame" there. Such calls are queued and run from the game's
 * message pump (PeekMessageA), once the graphics server is outside a frame. */
#include "game.h"

#define MAX_DEFERRED 8
typedef struct Deferred {
    void (*fn)(const char *a, const char *b);
    char a[128], b[160];
} Deferred;

static Deferred g_queue[MAX_DEFERRED];
static int g_count;
static void **g_slot;
static BOOL (WINAPI *prev_PeekMessageA)(MSG *, HWND, UINT, UINT, UINT);

static LONG g_pumpCalls, g_pumpOutside;

int defer_stats(char *out, int outsz)
{
    return snprintf(out, outsz, "pump calls=%ld outside frame=%ld queued=%d hooked=%d", g_pumpCalls,
                    g_pumpOutside, g_count, g_slot != NULL);
}

static int outside_frame(void)
{
    BYTE *gfx;
    if (!game_ok()) return 0;
    gfx = *(BYTE **)ADDR_NGFXSERVER_PTR;
    return gfx && game_readable(gfx + GFX_IN_BEGIN_FRAME, 1) && !gfx[GFX_IN_BEGIN_FRAME];
}

static void pump(void)
{
    int i, n;
    Deferred run[MAX_DEFERRED];
    g_pumpCalls++;
    if (!outside_frame()) return;
    g_pumpOutside++;
    if (!g_count) return;
    n = g_count;
    memcpy(run, g_queue, n * sizeof run[0]);
    g_count = 0;
    for (i = 0; i < n; i++) run[i].fn(run[i].a, run[i].b);
}

static BOOL WINAPI hk_PeekMessageA(MSG *m, HWND w, UINT mn, UINT mx, UINT remove)
{
    pump();
    return prev_PeekMessageA(m, w, mn, mx, remove);
}

int defer_call(void (*fn)(const char *a, const char *b), const char *a, const char *b)
{
    Deferred *d;
    if (!g_slot || g_count >= MAX_DEFERRED) return 0;
    d = &g_queue[g_count++];
    d->fn = fn;
    strncpy(d->a, a ? a : "", sizeof d->a - 1);
    d->a[sizeof d->a - 1] = 0;
    strncpy(d->b, b ? b : "", sizeof d->b - 1);
    d->b[sizeof d->b - 1] = 0;
    return 1;
}

void defer_init(void)
{
    g_slot = game_iat_slot("user32.dll", "PeekMessageA");
    if (!g_slot) {
        mlog("defer: PeekMessageA import not found");
        return;
    }
    prev_PeekMessageA = *g_slot;
    patch_ptr(g_slot, (void *)hk_PeekMessageA);
}

void defer_shutdown(void)
{
    if (g_slot && *g_slot == (void *)hk_PeekMessageA) patch_ptr(g_slot, (void *)prev_PeekMessageA);
    g_slot = NULL;
    g_count = 0;
}
