/* Loads _mod\drakensang_mod.dll (the core) from a private copy so the original can be rebuilt,
 * and reloads it when the file _mod\reload appears. */
#include "loader.h"

ModCore g_core;
int g_coreLoaded;

static HMODULE g_coreMod;
static WCHAR g_coreCopy[MAX_PATH];
static unsigned g_loadCount;
static ModHost g_hostApi;

static void delete_stale_copies(const WCHAR *dir)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wcscpy(pattern, dir);
    wcscat(pattern, L"loaded_core_*.dll");
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wcscpy(path, dir);
        wcscat(path, fd.cFileName);
        DeleteFileW(path);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int core_load(void)
{
    WCHAR src[MAX_PATH], dir[MAX_PATH];
    PFN_ModCoreInit init;

    g_hostApi.version = MOD_HOST_VERSION;
    g_hostApi.gameDir = g_gameDir;
    g_hostApi.modDir = g_modDir;
    g_hostApi.dev = &g_dev;
    g_hostApi.hwnd = &g_hwnd;
    g_hostApi.blockKeyboard = &g_blockKeyboard;
    g_hostApi.blockMouse = &g_blockMouse;
    g_hostApi.take_mouse = input_take_mouse;
    g_hostApi.input_stats = input_stats;

    wcscpy(src, g_modDir);
    wcscat(src, L"drakensang_mod.dll");
    wcscpy(dir, g_modDir);
    wcscat(dir, L"build\\");
    CreateDirectoryW(dir, NULL);
    _snwprintf(g_coreCopy, MAX_PATH, L"%sloaded_core_%u_%lu.dll", dir, ++g_loadCount, GetTickCount());
    if (!CopyFileW(src, g_coreCopy, FALSE)) {
        mlog("core: cannot copy drakensang_mod.dll (error %lu)", GetLastError());
        return 0;
    }
    g_coreMod = LoadLibraryW(g_coreCopy);
    init = g_coreMod ? (PFN_ModCoreInit)GetProcAddress(g_coreMod, "ModCoreInit") : NULL;
    memset(&g_core, 0, sizeof g_core);
    if (!init || !init(&g_hostApi, &g_core)) {
        mlog("core: load failed (module %p, init %p, error %lu)", g_coreMod, init, GetLastError());
        if (g_coreMod) FreeLibrary(g_coreMod);
        g_coreMod = NULL;
        DeleteFileW(g_coreCopy);
        return 0;
    }
    g_coreLoaded = 1;
    mlog("core: loaded #%u", g_loadCount);
    if (g_dev && g_core.device_created) g_core.device_created(g_dev);
    return 1;
}

static void core_unload(void)
{
    if (!g_coreMod) return;
    if (g_coreLoaded && g_core.shutdown) g_core.shutdown();
    g_coreLoaded = 0;
    memset(&g_core, 0, sizeof g_core);
    g_blockKeyboard = g_blockMouse = 0;
    FreeLibrary(g_coreMod);
    g_coreMod = NULL;
    DeleteFileW(g_coreCopy);
    mlog("core: unloaded");
}

void core_ensure_loaded(void)
{
    static int tried;
    WCHAR dir[MAX_PATH];
    if (tried) return;
    tried = 1;
    wcscpy(dir, g_modDir);
    wcscat(dir, L"build\\");
    delete_stale_copies(dir);
    core_load();
}

void core_poll_reload(void)
{
    static double next;
    WCHAR path[MAX_PATH];
    double now = now_seconds();
    if (now < next) return;
    next = now + 0.5;
    wcscpy(path, g_modDir);
    wcscat(path, L"reload");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileW(path);
    mlog("core: reload requested");
    core_unload();
    core_load();
}
