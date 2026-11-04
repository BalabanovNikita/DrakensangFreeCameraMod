/* Input filtering: DirectInput8 devices (mouse motion, buttons) and window messages (keyboard).
 * While g_blockKeyboard / g_blockMouse are set the game receives nothing but releases of keys
 * and buttons it already considers held, so nothing stays stuck. */
#include "loader.h"

volatile int g_blockKeyboard, g_blockMouse;

static void *g_kbDev, *g_mouseDev;
static volatile LONG g_mdx, g_mdy, g_mdz;

static BYTE g_diKeys[256];      /* DI keyboard keys the game sees as held */
static BYTE g_diButtons[8];     /* DI mouse buttons the game sees as held */
static BYTE g_msgKeys[256];     /* VK keys held according to delivered WM_KEYDOWN/UP */
static BYTE g_msgButtons[5];    /* L R M X1 X2 from delivered button messages */

static LONG g_cntState[2], g_cntData[2], g_cntKeyMsg, g_cntMouseMsg;

static const GUID kGUID_SysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kGUID_SysMouse    = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

typedef HRESULT (WINAPI *PFN_CreateDevice)(void *self, REFGUID guid, void **dev, LPUNKNOWN outer);
typedef HRESULT (WINAPI *PFN_GetDeviceState)(void *self, DWORD cb, void *data);
typedef HRESULT (WINAPI *PFN_GetDeviceData)(void *self, DWORD cb, DIDEVICEOBJECTDATA *rg, DWORD *inout, DWORD flags);

/* vtable slots (IDirectInput8 / IDirectInputDevice8) */
enum { VT_DI_CREATEDEVICE = 3, VT_DEV_GETDEVICESTATE = 9, VT_DEV_GETDEVICEDATA = 10 };

#define MAXVT 4
static struct { void **vtbl; PFN_CreateDevice createDevice; } g_diVt[MAXVT];
static struct { void **vtbl; PFN_GetDeviceState getState; PFN_GetDeviceData getData; } g_devVt[MAXVT];

static int dev_vt_index(void *self)
{
    void **vtbl = *(void ***)self;
    int i;
    for (i = 0; i < MAXVT; i++)
        if (g_devVt[i].vtbl == vtbl) return i;
    return 0;
}

static HRESULT WINAPI hk_GetDeviceState(void *self, DWORD cb, void *data)
{
    HRESULT hr = g_devVt[dev_vt_index(self)].getState(self, cb, data);
    if (FAILED(hr) || !data) return hr;
    if (self == g_mouseDev && cb >= sizeof(DIMOUSESTATE)) {
        DIMOUSESTATE *m = (DIMOUSESTATE *)data;
        g_cntState[1]++;
        InterlockedExchangeAdd(&g_mdx, m->lX);
        InterlockedExchangeAdd(&g_mdy, m->lY);
        InterlockedExchangeAdd(&g_mdz, m->lZ);
        if (g_blockMouse) memset(data, 0, cb);
    } else if (self == g_kbDev) {
        g_cntState[0]++;
        if (g_blockKeyboard) memset(data, 0, cb);
    }
    return hr;
}

static DWORD emit_release(BYTE *held, int count, DWORD baseOfs, DIDEVICEOBJECTDATA *rg, DWORD cb, DWORD cap)
{
    DWORD n = 0;
    int i;
    for (i = 0; i < count && n < cap; i++) {
        DIDEVICEOBJECTDATA *e;
        if (!held[i]) continue;
        e = (DIDEVICEOBJECTDATA *)((BYTE *)rg + n * cb);
        memset(e, 0, cb);
        e->dwOfs = baseOfs + i;
        e->dwTimeStamp = GetTickCount();
        held[i] = 0;
        n++;
    }
    return n;
}

static HRESULT WINAPI hk_GetDeviceData(void *self, DWORD cb, DIDEVICEOBJECTDATA *rg, DWORD *inout, DWORD flags)
{
    DWORD cap = inout ? *inout : 0;
    HRESULT hr = g_devVt[dev_vt_index(self)].getData(self, cb, rg, inout, flags);
    DWORD i, n;
    if (FAILED(hr) || !inout || !rg || (flags & DIGDD_PEEK) || cb < 16) return hr;
    n = *inout;

    if (self == g_mouseDev) {
        g_cntData[1]++;
        for (i = 0; i < n; i++) {
            DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((BYTE *)rg + i * cb);
            if (e->dwOfs == DIMOFS_X) InterlockedExchangeAdd(&g_mdx, (LONG)e->dwData);
            else if (e->dwOfs == DIMOFS_Y) InterlockedExchangeAdd(&g_mdy, (LONG)e->dwData);
            else if (e->dwOfs == DIMOFS_Z) InterlockedExchangeAdd(&g_mdz, (LONG)e->dwData);
            else if (!g_blockMouse && e->dwOfs >= DIMOFS_BUTTON0 && e->dwOfs <= DIMOFS_BUTTON7)
                g_diButtons[e->dwOfs - DIMOFS_BUTTON0] = (e->dwData & 0x80) ? 1 : 0;
        }
        if (g_blockMouse) *inout = emit_release(g_diButtons, 8, DIMOFS_BUTTON0, rg, cb, cap);
    } else if (self == g_kbDev) {
        g_cntData[0]++;
        if (g_blockKeyboard) {
            *inout = emit_release(g_diKeys, 256, 0, rg, cb, cap);
        } else {
            for (i = 0; i < n; i++) {
                DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((BYTE *)rg + i * cb);
                g_diKeys[e->dwOfs & 0xFF] = (e->dwData & 0x80) ? 1 : 0;
            }
        }
    }
    return hr;
}

static void hook_device(void *dev)
{
    void **vtbl = *(void ***)dev;
    int i, freeSlot = -1;
    for (i = 0; i < MAXVT; i++) {
        if (g_devVt[i].vtbl == vtbl) return;
        if (!g_devVt[i].vtbl && freeSlot < 0) freeSlot = i;
    }
    if (freeSlot < 0) return;
    g_devVt[freeSlot].getState = (PFN_GetDeviceState)vtbl[VT_DEV_GETDEVICESTATE];
    g_devVt[freeSlot].getData = (PFN_GetDeviceData)vtbl[VT_DEV_GETDEVICEDATA];
    g_devVt[freeSlot].vtbl = vtbl;
    patch_ptr(&vtbl[VT_DEV_GETDEVICESTATE], (void *)hk_GetDeviceState);
    patch_ptr(&vtbl[VT_DEV_GETDEVICEDATA], (void *)hk_GetDeviceData);
    mlog("input: device vtable %p hooked", vtbl);
}

static HRESULT WINAPI hk_CreateDevice(void *self, REFGUID guid, void **dev, LPUNKNOWN outer)
{
    void **vtbl = *(void ***)self;
    PFN_CreateDevice orig = NULL;
    HRESULT hr;
    int i;
    for (i = 0; i < MAXVT; i++)
        if (g_diVt[i].vtbl == vtbl) orig = g_diVt[i].createDevice;
    hr = orig(self, guid, dev, outer);
    if (SUCCEEDED(hr) && dev && *dev) {
        if (IsEqualGUID(guid, &kGUID_SysKeyboard)) {
            g_kbDev = *dev;
            hook_device(*dev);
            mlog("input: keyboard device %p", *dev);
        } else if (IsEqualGUID(guid, &kGUID_SysMouse)) {
            g_mouseDev = *dev;
            hook_device(*dev);
            mlog("input: mouse device %p", *dev);
        }
    }
    return hr;
}

void input_wrap_dinput(void *di)
{
    void **vtbl = *(void ***)di;
    int i, freeSlot = -1;
    for (i = 0; i < MAXVT; i++) {
        if (g_diVt[i].vtbl == vtbl) return;
        if (!g_diVt[i].vtbl && freeSlot < 0) freeSlot = i;
    }
    if (freeSlot < 0) return;
    g_diVt[freeSlot].createDevice = (PFN_CreateDevice)vtbl[VT_DI_CREATEDEVICE];
    g_diVt[freeSlot].vtbl = vtbl;
    patch_ptr(&vtbl[VT_DI_CREATEDEVICE], (void *)hk_CreateDevice);
    mlog("input: IDirectInput8 vtable %p hooked", vtbl);
}

void input_take_mouse(int *dx, int *dy, int *dz)
{
    *dx = InterlockedExchange(&g_mdx, 0);
    *dy = InterlockedExchange(&g_mdy, 0);
    *dz = InterlockedExchange(&g_mdz, 0);
}

void input_stats(char *out, int outsz)
{
    snprintf(out, outsz, "di kb state=%ld data=%ld, di mouse state=%ld data=%ld, key msgs=%ld, mouse msgs=%ld",
             g_cntState[0], g_cntData[0], g_cntState[1], g_cntData[1], g_cntKeyMsg, g_cntMouseMsg);
}

/* ---- window messages ----------------------------------------------------- */
static BOOL (WINAPI *real_PeekMessageA)(MSG *, HWND, UINT, UINT, UINT);
static BOOL (WINAPI *real_GetMessageA)(MSG *, HWND, UINT, UINT);

static int button_index(UINT msg, WPARAM wp, int *down)
{
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: *down = 1; return 0;
    case WM_LBUTTONUP: *down = 0; return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: *down = 1; return 1;
    case WM_RBUTTONUP: *down = 0; return 1;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: *down = 1; return 2;
    case WM_MBUTTONUP: *down = 0; return 2;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: *down = 1; return HIWORD(wp) == XBUTTON2 ? 4 : 3;
    case WM_XBUTTONUP: *down = 0; return HIWORD(wp) == XBUTTON2 ? 4 : 3;
    }
    return -1;
}

/* Decide whether a message may reach the game, and track held keys/buttons. */
static int allow_message(const MSG *m)
{
    UINT msg = m->message;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        int vk = (int)(m->wParam & 0xFF), down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        g_cntKeyMsg++;
        if (g_blockKeyboard) {
            if (!down && g_msgKeys[vk]) {   /* let releases of held keys through */
                g_msgKeys[vk] = 0;
                return 1;
            }
            return 0;
        }
        g_msgKeys[vk] = (BYTE)down;
        return 1;
    }
    if (msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_DEADCHAR || msg == WM_SYSDEADCHAR)
        return !g_blockKeyboard;
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        g_cntMouseMsg++;
        return !g_blockMouse;
    }
    {
        int down, b = button_index(msg, m->wParam, &down);
        if (b >= 0) {
            g_cntMouseMsg++;
            if (g_blockMouse) {
                if (!down && g_msgButtons[b]) {
                    g_msgButtons[b] = 0;
                    return 1;
                }
                return 0;
            }
            g_msgButtons[b] = (BYTE)down;
        }
    }
    return 1;
}

static BOOL WINAPI hk_PeekMessageA(MSG *m, HWND w, UINT mn, UINT mx, UINT remove)
{
    for (;;) {
        BOOL r = real_PeekMessageA(m, w, mn, mx, remove);
        if (!r || allow_message(m)) return r;
        if (!(remove & PM_REMOVE)) {   /* drop it from the queue, then look again */
            MSG tmp;
            real_PeekMessageA(&tmp, m->hwnd, m->message, m->message, PM_REMOVE | (remove & ~PM_REMOVE));
        }
    }
}

static BOOL WINAPI hk_GetMessageA(MSG *m, HWND w, UINT mn, UINT mx)
{
    for (;;) {
        BOOL r = real_GetMessageA(m, w, mn, mx);
        if (r <= 0 || allow_message(m)) return r;
    }
}

void input_install_message_filter(HMODULE exe)
{
    real_PeekMessageA = iat_hook(exe, "user32.dll", "PeekMessageA", (void *)hk_PeekMessageA);
    real_GetMessageA = iat_hook(exe, "user32.dll", "GetMessageA", (void *)hk_GetMessageA);
    mlog("IAT: PeekMessageA %s, GetMessageA %s", real_PeekMessageA ? "hooked" : "NOT found",
         real_GetMessageA ? "hooked" : "NOT found");
}
