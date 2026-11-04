/* On-screen text: short status messages and the menu box (ID3DXFont). */
#include "core.h"

OverlayMenu g_menu;

typedef HRESULT (WINAPI *PFN_D3DXCreateFontW)(IDirect3DDevice9 *, INT, UINT, UINT, UINT, BOOL, DWORD, DWORD,
                                              DWORD, DWORD, const WCHAR *, ID3DXFont **);
static PFN_D3DXCreateFontW pD3DXCreateFontW;
static ID3DXFont *g_font;
static IDirect3DStateBlock9 *g_sb;
static IDirect3DDevice9 *g_odev;
static int g_bbW, g_bbH, g_fontH;

#define NTOASTS 4
static struct { char text[160]; double until; } g_toasts[NTOASTS];

void toast(const char *fmt, ...)
{
    va_list ap;
    int i, slot = 0;
    for (i = 0; i < NTOASTS; i++)
        if (g_toasts[i].until < g_toasts[slot].until) slot = i;
    va_start(ap, fmt);
    vsnprintf(g_toasts[slot].text, sizeof g_toasts[slot].text, fmt, ap);
    va_end(ap);
    g_toasts[slot].until = now_seconds() + 2.5;
    mlog("toast: %s", g_toasts[slot].text);
}

static void create_resources(void)
{
    IDirect3DSurface9 *bb;
    D3DSURFACE_DESC d;
    HRESULT hr;
    if (!g_odev) return;
    if (SUCCEEDED(IDirect3DDevice9_GetBackBuffer(g_odev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        IDirect3DSurface9_GetDesc(bb, &d);
        g_bbW = d.Width;
        g_bbH = d.Height;
        IDirect3DSurface9_Release(bb);
    }
    if (!pD3DXCreateFontW) {
        HMODULE d3dx = GetModuleHandleW(L"d3dx9_39.dll");
        if (!d3dx) d3dx = LoadLibraryW(L"d3dx9_39.dll");
        if (d3dx) pD3DXCreateFontW = (PFN_D3DXCreateFontW)GetProcAddress(d3dx, "D3DXCreateFontW");
    }
    g_fontH = (int)((g_bbH > 0 ? g_bbH / 40 : 20) * (g_cfg.font_scale > 0.2f ? g_cfg.font_scale : 1.0f));
    if (g_fontH < 14) g_fontH = 14;
    if (pD3DXCreateFontW && !g_font) {
        hr = pD3DXCreateFontW(g_odev, g_fontH, 0, FW_SEMIBOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial", &g_font);
        if (FAILED(hr)) g_font = NULL;
    }
    if (!g_sb) {
        hr = IDirect3DDevice9_CreateStateBlock(g_odev, D3DSBT_ALL, &g_sb);
        if (FAILED(hr)) g_sb = NULL;
    }
    mlog("overlay: %dx%d font %p (h=%d) stateblock %p", g_bbW, g_bbH, g_font, g_fontH, g_sb);
}

void overlay_device_created(IDirect3DDevice9 *dev)
{
    if (g_font) {
        ID3DXFont_Release(g_font);
        g_font = NULL;
    }
    if (g_sb) {
        IDirect3DStateBlock9_Release(g_sb);
        g_sb = NULL;
    }
    g_odev = dev;
    create_resources();
    toast("Drakensang mod " MOD_VERSION " loaded");
}

void overlay_shutdown(void)
{
    if (g_font) ID3DXFont_Release(g_font);
    if (g_sb) IDirect3DStateBlock9_Release(g_sb);
    g_font = NULL;
    g_sb = NULL;
    g_odev = NULL;
}

void overlay_device_lost(void)
{
    if (g_font) ID3DXFont_OnLostDevice(g_font);
    if (g_sb) {
        IDirect3DStateBlock9_Release(g_sb);
        g_sb = NULL;
    }
}

void overlay_device_reset(void)
{
    if (g_font) {   /* size may have changed: recreate with the new height */
        ID3DXFont_Release(g_font);
        g_font = NULL;
    }
    create_resources();
}

typedef struct { float x, y, z, rhw; DWORD c; } QuadVertex;

static void fill_rect(IDirect3DDevice9 *dev, float x0, float y0, float x1, float y1, DWORD c)
{
    QuadVertex v[4] = {
        {x0 - 0.5f, y0 - 0.5f, 0, 1, c}, {x1 - 0.5f, y0 - 0.5f, 0, 1, c},
        {x0 - 0.5f, y1 - 0.5f, 0, 1, c}, {x1 - 0.5f, y1 - 0.5f, 0, 1, c},
    };
    IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(QuadVertex));
}

static int text_width(const WCHAR *w)
{
    RECT r = {0, 0, 0, 0};
    ID3DXFont_DrawTextW(g_font, NULL, w, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOCLIP, 0);
    return r.right - r.left;
}

static void draw_text(const WCHAR *w, int x, int y, DWORD color)
{
    RECT r = {x + 1, y + 1, x + 4000, y + 400};
    ID3DXFont_DrawTextW(g_font, NULL, w, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP, 0xC0000000);
    r.left = x;
    r.top = y;
    ID3DXFont_DrawTextW(g_font, NULL, w, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP, color);
}

static void to_wide(const char *s, WCHAR *w, int n)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) w[0] = 0;
}

static void setup_states(IDirect3DDevice9 *dev)
{
    D3DVIEWPORT9 vp = {0, 0, (DWORD)g_bbW, (DWORD)g_bbH, 0.0f, 1.0f};
    IDirect3DDevice9_SetViewport(dev, &vp);
    IDirect3DDevice9_SetVertexShader(dev, NULL);
    IDirect3DDevice9_SetPixelShader(dev, NULL);
    IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_SetTexture(dev, 0, NULL);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_STENCILENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xF);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SRGBWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_FILLMODE, D3DFILL_SOLID);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CLIPPING, TRUE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

static void draw_menu(IDirect3DDevice9 *dev)
{
    static WCHAR w[256];
    int pad = g_fontH / 2, lineH = g_fontH + g_fontH / 4;
    int boxW = g_bbW * 3 / 5, x0, y0, x1, y1, i, y;
    int n = g_menu.nlines;
    if (boxW < 400) boxW = g_bbW - 20;
    x0 = (g_bbW - boxW) / 2;
    x1 = x0 + boxW;
    y0 = g_bbH / 8;
    y1 = y0 + pad * 2 + lineH * (n + 2) + pad;
    fill_rect(dev, (float)x0, (float)y0, (float)x1, (float)y1, 0xD0101418);
    fill_rect(dev, (float)x0, (float)y0, (float)x1, (float)(y0 + lineH + pad), 0xE0283848);
    to_wide(g_menu.title, w, 256);
    draw_text(w, x0 + pad, y0 + pad / 2, 0xFFFFE8A0);
    y = y0 + lineH + pad + pad / 2;
    for (i = 0; i < n; i++, y += lineH) {
        if (i == g_menu.selected)
            fill_rect(dev, (float)(x0 + pad / 2), (float)(y - 2), (float)(x1 - pad / 2), (float)(y + lineH - 2),
                      0xC0406080);
        to_wide(g_menu.lines[i], w, 256);
        {
            /* "label\tvalue": the value is right-aligned in a column at mid-width */
            WCHAR *tab = wcschr(w, L'\t');
            DWORD c = i == g_menu.selected ? 0xFFFFFFFF : 0xFFC8C8C8;
            if (tab) *tab++ = 0;
            draw_text(w, x0 + pad, y, c);
            if (tab) draw_text(tab, x0 + boxW / 2 - text_width(tab), y, c);
        }
    }
    to_wide(g_menu.footer, w, 256);
    draw_text(w, x0 + pad, y + pad / 2, 0xFF90A0B0);
}

static void draw_toasts(IDirect3DDevice9 *dev, double now)
{
    static WCHAR w[200];
    int i, y = g_bbH / 20, lineH = g_fontH + g_fontH / 2;
    for (i = 0; i < NTOASTS; i++) {
        int tw, x;
        if (g_toasts[i].until < now) continue;
        to_wide(g_toasts[i].text, w, 200);
        tw = text_width(w);
        x = (g_bbW - tw) / 2;
        fill_rect(dev, (float)(x - g_fontH / 2), (float)(y - g_fontH / 6), (float)(x + tw + g_fontH / 2),
                  (float)(y + g_fontH + g_fontH / 6), 0xB0000000);
        draw_text(w, x, y, 0xFFFFFFFF);
        y += lineH;
    }
}

void overlay_draw(IDirect3DDevice9 *dev)
{
    double now = now_seconds();
    int i, anyToast = 0, showToasts;
    IDirect3DSurface9 *oldRT = NULL, *oldDS = NULL, *bb = NULL;

    if (!g_font || !g_sb || dev != g_odev) return;
    showToasts = g_cfg.toasts && (!g_uiHidden || g_cfg.toasts_when_hidden);
    for (i = 0; i < NTOASTS; i++)
        if (g_toasts[i].until >= now) anyToast = 1;
    if (!(showToasts && anyToast) && !g_menu.visible) return;

    IDirect3DStateBlock9_Capture(g_sb);
    IDirect3DDevice9_GetRenderTarget(dev, 0, &oldRT);
    IDirect3DDevice9_GetDepthStencilSurface(dev, &oldDS);
    if (SUCCEEDED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
        IDirect3DDevice9_SetDepthStencilSurface(dev, NULL);
        if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
            setup_states(dev);
            if (g_menu.visible) draw_menu(dev);
            if (showToasts) draw_toasts(dev, now);
            IDirect3DDevice9_EndScene(dev);
        }
        IDirect3DSurface9_Release(bb);
    }
    if (oldRT) {
        IDirect3DDevice9_SetRenderTarget(dev, 0, oldRT);
        IDirect3DSurface9_Release(oldRT);
    }
    IDirect3DDevice9_SetDepthStencilSurface(dev, oldDS);
    if (oldDS) IDirect3DSurface9_Release(oldDS);
    IDirect3DStateBlock9_Apply(g_sb);
}
