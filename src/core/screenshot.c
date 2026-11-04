/* PNG screenshots of the back buffer, taken before the mod's overlay is drawn. */
#include "core.h"

static int   g_pending, g_withOverlay;
static WCHAR g_path[MAX_PATH];

void shot_request_overlay(const WCHAR *path)
{
    shot_request(path);
    g_withOverlay = 1;
}

void shot_request(const WCHAR *path)
{
    g_withOverlay = 0;
    if (path) {
        wcscpy(g_path, path);
    } else {
        SYSTEMTIME t;
        WCHAR dir[MAX_PATH];
        GetLocalTime(&t);
        wcscpy(dir, g_gameDir);
        wcscat(dir, L"screenshots\\");
        CreateDirectoryW(dir, NULL);
        _snwprintf(g_path, MAX_PATH, L"%sdrakensang_%04d-%02d-%02d_%02d-%02d-%02d-%03d.png", dir, t.wYear,
                   t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    }
    g_pending = 1;
}

typedef HRESULT (WINAPI *PFN_D3DXSaveSurfaceToFileW)(const WCHAR *, D3DXIMAGE_FILEFORMAT, IDirect3DSurface9 *,
                                                      const PALETTEENTRY *, const RECT *);

static int write_bmp(const WCHAR *path, IDirect3DSurface9 *sys, UINT w, UINT h)
{
    D3DLOCKED_RECT lr;
    HANDLE f;
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    static BYTE row[4 * 8192];
    DWORD stride = (w * 3 + 3) & ~3u, written;
    int y;
    if (w > 8192) return 0;
    if (FAILED(IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY))) return 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        IDirect3DSurface9_UnlockRect(sys);
        return 0;
    }
    memset(&fh, 0, sizeof fh);
    memset(&ih, 0, sizeof ih);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + stride * h;
    ih.biSize = sizeof ih;
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    WriteFile(f, &fh, sizeof fh, &written, NULL);
    WriteFile(f, &ih, sizeof ih, &written, NULL);
    for (y = (int)h - 1; y >= 0; y--) {
        const BYTE *src = (const BYTE *)lr.pBits + y * lr.Pitch;
        UINT x;
        memset(row, 0, stride);
        for (x = 0; x < w; x++) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        WriteFile(f, row, stride, &written, NULL);
    }
    CloseHandle(f);
    IDirect3DSurface9_UnlockRect(sys);
    return 1;
}

void shot_take_if_pending(IDirect3DDevice9 *dev, int afterOverlay)
{
    IDirect3DSurface9 *bb = NULL, *resolved = NULL, *sys = NULL, *src;
    D3DSURFACE_DESC d;
    HRESULT hr;
    int ok = 0;
    PFN_D3DXSaveSurfaceToFileW save;
    HMODULE d3dx;

    if (!g_pending || afterOverlay != g_withOverlay) return;
    g_pending = 0;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        mlog("screenshot: GetBackBuffer failed");
        return;
    }
    IDirect3DSurface9_GetDesc(bb, &d);
    src = bb;
    if (d.MultiSampleType != D3DMULTISAMPLE_NONE) {
        if (SUCCEEDED(IDirect3DDevice9_CreateRenderTarget(dev, d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE,
                                                          0, FALSE, &resolved, NULL)) &&
            SUCCEEDED(IDirect3DDevice9_StretchRect(dev, bb, NULL, resolved, NULL, D3DTEXF_NONE)))
            src = resolved;
    }
    hr = IDirect3DDevice9_CreateOffscreenPlainSurface(dev, d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, NULL);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_GetRenderTargetData(dev, src, sys);
    if (FAILED(hr)) {
        mlog("screenshot: readback failed %08lx (fmt %d, %ux%u)", hr, d.Format, d.Width, d.Height);
        goto done;
    }
    if (d.Format == D3DFMT_A8R8G8B8) {   /* force opaque so the PNG has no holes */
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(IDirect3DSurface9_LockRect(sys, &lr, NULL, 0))) {
            UINT x, y;
            for (y = 0; y < d.Height; y++) {
                BYTE *p = (BYTE *)lr.pBits + y * lr.Pitch;
                for (x = 0; x < d.Width; x++) p[x * 4 + 3] = 0xFF;
            }
            IDirect3DSurface9_UnlockRect(sys);
        }
    }
    d3dx = GetModuleHandleW(L"d3dx9_39.dll");
    save = d3dx ? (PFN_D3DXSaveSurfaceToFileW)GetProcAddress(d3dx, "D3DXSaveSurfaceToFileW") : NULL;
    if (save) {
        hr = save(g_path, D3DXIFF_PNG, sys, NULL, NULL);
        ok = SUCCEEDED(hr);
        if (!ok) mlog("screenshot: D3DXSaveSurfaceToFileW failed %08lx, falling back to BMP", hr);
    }
    if (!ok && (d.Format == D3DFMT_A8R8G8B8 || d.Format == D3DFMT_X8R8G8B8)) {
        WCHAR *ext = wcsrchr(g_path, L'.');
        if (ext) wcscpy(ext, L".bmp");
        ok = write_bmp(g_path, sys, d.Width, d.Height);
    }
    {
        char p8[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, g_path, -1, p8, sizeof p8, NULL, NULL);
        mlog("screenshot %s: %s (%ux%u fmt %d)", ok ? "saved" : "FAILED", p8, d.Width, d.Height, d.Format);
        if (ok) toast("Screenshot saved");
    }
done:
    if (sys) IDirect3DSurface9_Release(sys);
    if (resolved) IDirect3DSurface9_Release(resolved);
    IDirect3DSurface9_Release(bb);
}
