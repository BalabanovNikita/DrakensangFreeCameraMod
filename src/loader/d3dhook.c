/* Direct3D 9 hooks: device creation, Present (per-frame tick for the core), Reset. */
#include "loader.h"

IDirect3DDevice9 *g_dev;
HWND g_hwnd;
volatile LONG g_presentCount;

/* Exclusive fullscreen makes the game minimise and stall whenever focus is lost (Alt+Tab).
 * Present windowed instead: the game's window is already a popup covering the screen. */
static void force_windowed(D3DPRESENT_PARAMETERS *pp)
{
    if (!g_cfg.borderless || !pp || pp->Windowed) return;
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    mlog("display: exclusive fullscreen replaced by borderless window (%ux%u)", pp->BackBufferWidth,
         pp->BackBufferHeight);
}

static void make_borderless(HWND w)
{
    MONITORINFO mi;
    RECT r;
    LONG style;
    if (!g_cfg.borderless || !w) return;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoA(MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
    style = GetWindowLongA(w, GWL_STYLE);
    GetWindowRect(w, &r);
    if ((style & (WS_CAPTION | WS_THICKFRAME)) == 0 && EqualRect(&r, &mi.rcMonitor)) return;
    SetWindowLongA(w, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU)) | WS_POPUP | WS_VISIBLE);
    SetWindowPos(w, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    mlog("display: window %p made borderless", w);
}

typedef IDirect3D9 *(WINAPI *PFN_Direct3DCreate9)(UINT);
static PFN_Direct3DCreate9 real_Direct3DCreate9;
static HRESULT (STDMETHODCALLTYPE *real_CreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD,
                                                      D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static HRESULT (STDMETHODCALLTYPE *real_Present)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static HRESULT (STDMETHODCALLTYPE *real_Reset)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);

static HRESULT STDMETHODCALLTYPE hk_Present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd,
                                            const RGNDATA *dirty)
{
    static int first = 1;
    if (dev == g_dev) {
        InterlockedIncrement(&g_presentCount);
        if (first) {
            first = 0;
            mlog("first Present");
        }
        core_poll_reload();
        if (g_coreLoaded && g_core.present) g_core.present(dev);
    }
    return real_Present(dev, src, dst, wnd, dirty);
}

static HRESULT STDMETHODCALLTYPE hk_Reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    HRESULT hr;
    if (dev == g_dev && g_coreLoaded && g_core.device_lost) g_core.device_lost();
    force_windowed(pp);
    hr = real_Reset(dev, pp);
    mlog("Reset -> %08lx (%ux%u)", hr, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0);
    if (dev == g_dev && SUCCEEDED(hr) && g_coreLoaded && g_core.device_reset) g_core.device_reset();
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_CreateDevice(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus,
                                                 DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    HRESULT hr;
    force_windowed(pp);
    hr = real_CreateDevice(d3d, adapter, type, focus, flags, pp, out);
    mlog("CreateDevice -> %08lx flags %08lx %ux%u windowed=%d fmt=%d ms=%d", hr, flags,
         pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0, pp ? pp->Windowed : -1,
         pp ? pp->BackBufferFormat : -1, pp ? pp->MultiSampleType : -1);
    if (SUCCEEDED(hr) && out && *out) {
        IDirect3DDevice9 *dev = *out;
        IDirect3DDevice9Vtbl *vt = (IDirect3DDevice9Vtbl *)dev->lpVtbl;
        if ((void *)vt->Present != (void *)hk_Present) {
            real_Present = vt->Present;
            real_Reset = vt->Reset;
            patch_ptr((void **)&vt->Present, (void *)hk_Present);
            patch_ptr((void **)&vt->Reset, (void *)hk_Reset);
            mlog("device vtable %p hooked", vt);
        }
        g_dev = dev;
        g_hwnd = (pp && pp->hDeviceWindow) ? pp->hDeviceWindow : focus;
        make_borderless(g_hwnd);
        core_ensure_loaded();
        if (g_coreLoaded && g_core.device_created) g_core.device_created(dev);
    }
    return hr;
}

static IDirect3D9 *WINAPI hk_Direct3DCreate9(UINT sdk)
{
    IDirect3D9 *d3d = real_Direct3DCreate9(sdk);
    mlog("Direct3DCreate9(%u) -> %p", sdk, d3d);
    if (d3d) {
        IDirect3D9Vtbl *vt = (IDirect3D9Vtbl *)d3d->lpVtbl;
        if ((void *)vt->CreateDevice != (void *)hk_CreateDevice) {
            real_CreateDevice = vt->CreateDevice;
            patch_ptr((void **)&vt->CreateDevice, (void *)hk_CreateDevice);
        }
    }
    return d3d;
}

void *d3d_hook_create(void *realFn)
{
    real_Direct3DCreate9 = (PFN_Direct3DCreate9)realFn;
    return (void *)hk_Direct3DCreate9;
}
