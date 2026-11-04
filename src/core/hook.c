/* Minimal inline detours for known, verified function prologues.
 * The caller passes how many prologue bytes to relocate (whole, position-independent
 * instructions, >= 5) and their expected values. Everything is undone on core unload. */
#include "game.h"

#define MAX_DETOURS 16

static Detour *g_detours[MAX_DETOURS];
static int g_ndetours;

static void write_code(BYTE *dst, const BYTE *src, int len)
{
    DWORD old;
    VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(dst, src, len);
    VirtualProtect(dst, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, len);
}

int detour_install(Detour *d, DWORD target, const char *expect, void *fn, void **orig)
{
    BYTE patch[16];
    int len = 0, i;
    const char *s;
    if (d->active) return 1;
    /* expected prologue bytes define the relocated length */
    for (s = expect; *s;) {
        unsigned v;
        while (*s == ' ') s++;
        if (!*s) break;
        if (sscanf(s, "%2x", &v) != 1 || len >= 16) return 0;
        d->orig[len++] = (BYTE)v;
        s += 2;
    }
    if (len < 5 || !game_check(target, expect)) {
        mlog("detour %08lx: prologue mismatch, not installed", (unsigned long)target);
        return 0;
    }
    d->target = (BYTE *)target;
    d->len = len;
    d->tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!d->tramp) return 0;
    memcpy(d->tramp, d->orig, len);
    d->tramp[len] = 0xE9;
    *(DWORD *)(d->tramp + len + 1) = (DWORD)(d->target + len) - (DWORD)(d->tramp + len + 5);
    FlushInstructionCache(GetCurrentProcess(), d->tramp, 64);

    patch[0] = 0xE9;
    *(DWORD *)(patch + 1) = (DWORD)fn - (DWORD)(d->target + 5);
    for (i = 5; i < len; i++) patch[i] = 0x90;
    *orig = d->tramp;
    write_code(d->target, patch, len);
    d->active = 1;
    if (g_ndetours < MAX_DETOURS) g_detours[g_ndetours++] = d;
    mlog("detour %08lx installed (%d bytes)", (unsigned long)target, len);
    return 1;
}

void detour_remove(Detour *d)
{
    if (!d->active) return;
    write_code(d->target, d->orig, d->len);
    d->active = 0;
    /* the trampoline is released by detour_remove_all, after no caller can be inside it */
}

void detour_remove_all(void)
{
    int i;
    for (i = 0; i < g_ndetours; i++) {
        detour_remove(g_detours[i]);
        if (g_detours[i]->tramp) VirtualFree(g_detours[i]->tramp, 0, MEM_RELEASE);
        g_detours[i]->tramp = NULL;
    }
    g_ndetours = 0;
}
