/* Core entry point: ModCoreInit is called by the loader after every (re)load. */
#include "core.h"

int _fltused = 0;
const ModHost *g_host;

static void core_shutdown(void)
{
    mod_shutdown();
    overlay_shutdown();
    mlog("core: shutdown done");
    log_close();
}

static void core_present(IDirect3DDevice9 *dev)
{
    mod_tick();
    shot_take_if_pending(dev, 0);
    overlay_draw(dev);
    shot_take_if_pending(dev, 1);
}

__declspec(dllexport) int ModCoreInit(const ModHost *host, ModCore *core)
{
    if (!host || host->version != MOD_HOST_VERSION) return 0;
    g_host = host;
    wcscpy(g_gameDir, host->gameDir);
    wcscpy(g_modDir, host->modDir);
    config_load();
    log_open(0);
    mlog("core " MOD_VERSION " init");

    core->shutdown = core_shutdown;
    core->device_created = overlay_device_created;
    core->device_lost = overlay_device_lost;
    core->device_reset = overlay_device_reset;
    core->present = core_present;
    mod_init();
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst;
    (void)reason;
    (void)reserved;
    return TRUE;
}
