/* Small helpers for safely touching game memory. */
#include "game.h"

int game_check(DWORD addr, const char *hexbytes)
{
    const BYTE *p = (const BYTE *)addr;
    const char *s = hexbytes;
    while (*s) {
        unsigned v;
        while (*s == ' ') s++;
        if (!*s) break;
        if (sscanf(s, "%2x", &v) != 1) return 0;
        if (*p++ != (BYTE)v) {
            mlog("signature mismatch at %08lx (%s)", (unsigned long)addr, hexbytes);
            return 0;
        }
        s += 2;
    }
    return 1;
}

int game_readable(const void *p, DWORD size)
{
    MEMORY_BASIC_INFORMATION mbi;
    const BYTE *b = (const BYTE *)p;
    if ((DWORD)b < 0x10000) return 0;
    while (size) {
        DWORD avail;
        if (!VirtualQuery(b, &mbi, sizeof mbi)) return 0;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
        avail = (DWORD)((const BYTE *)mbi.BaseAddress + mbi.RegionSize - b);
        if (avail >= size) return 1;
        b += avail;
        size -= avail;
    }
    return 1;
}

/* IAT slot of an import of the game exe. */
void **game_iat_slot(const char *dll, const char *func)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *d;
    if (!dir.VirtualAddress) return NULL;
    for (d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        IMAGE_THUNK_DATA *oft, *ft;
        if (_stricmp((char *)base + d->Name, dll) != 0 || !d->OriginalFirstThunk) continue;
        oft = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        ft = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) continue;
            if (!strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(base + oft->u1.AddressOfData))->Name, func))
                return (void **)&ft->u1.Function;
        }
    }
    return NULL;
}

/* Data files may exist per exe (The River of Time ships two exes with different worlds). */
void game_data_file(WCHAR *path, const WCHAR *base)
{
    WCHAR exe[MAX_PATH], *name, *dot;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    if ((dot = wcsrchr(name, L'.'))) *dot = 0;
    wcscpy(path, g_gameDir);
    wcscat(path, base);
    wcscat(path, L"_");
    wcscat(path, name);
    wcscat(path, L".txt");
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    wcscpy(path, g_gameDir);
    wcscat(path, base);
    wcscat(path, L".txt");
}

/* ---- game profiles ------------------------------------------------------- */
static const GameAddrs g_profiles[] = {
#include "profiles.inc"
};

const GameAddrs *G;
static int g_state = -1;

static void select_profile(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage;
    WCHAR exe[MAX_PATH], *name;
    unsigned i;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    g_state = 0;
    for (i = 0; i < sizeof g_profiles / sizeof g_profiles[0]; i++) {
        const GameAddrs *p = &g_profiles[i];
        int k, bad = 0;
        if (_wcsicmp(name, p->exe) || size != p->image_size || base != (BYTE *)0x400000) continue;
        for (k = 0; k < (int)(sizeof p->sigs / sizeof p->sigs[0]) && p->sigs[k].addr; k++)
            if (!game_check(p->sigs[k].addr, p->sigs[k].bytes)) bad++;
        mlog("game profile: %s, %d signatures, %d mismatched", p->name, k, bad);
        if (!bad) {
            G = p;
            g_state = 1;
        }
        return;
    }
    mlog("game profile: unsupported build (%ls, image %p, size %lx)", name, base, (unsigned long)size);
}

/* Every game-specific feature checks this first: the exe matches a known profile and all its
 * anchor signatures are intact. Otherwise nothing in the game's memory is touched. */
int game_ok(void)
{
    if (g_state < 0) select_profile();
    return g_state;
}

int game_is_d1(void)
{
    return game_ok() && G->ui_mode == UI_PASSES_D1;
}
