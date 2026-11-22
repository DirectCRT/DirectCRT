/*
 * ddtest - a DirectDraw test program for checking scaling and CRT shading.
 *
 * A control panel sets the display mode (any width and height, 8/16/24/32-bit),
 * how frames are presented and which test pattern to show. A separate test
 * window is where DirectDraw draws. It loads whichever ddraw.dll Windows finds
 * first, so put it next to the ddraw.dll you want to test, or on its own to
 * compare with Windows' own DirectDraw.
 *
 * Command line (all optional):
 *   --size WxH  --width N  --height N  --bpp 8|16|24|32  --refresh HZ  --fps N
 *   --present flip|blt|lock|windowed  --pattern NAME|NUMBER  --image FILE  --fit
 *   --start          apply the settings straight away
 *   --dump DIR       save every pattern (or just --pattern) as PNG files and exit,
 *                    without using DirectDraw; --frame N picks the animation frame
 *   --frames N       with --start: exit after N frames (exit code 0), or with
 *                    exit code 3 if DirectDraw failed
 *   --log FILE       with --frames: write the result to FILE
 */

#include "ddtest.h"

#include <commdlg.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initguid.h>
#include <ddraw.h>

/* ---------------------------------------------------------------------- */
/* Settings                                                                */

enum { PRESENT_FLIP, PRESENT_BLT, PRESENT_LOCK, PRESENT_WINDOWED, PRESENT_COUNT };

static const char* PRESENT_NAMES[PRESENT_COUNT] = {
    "Fullscreen: page flipping (primary + back buffer)",
    "Fullscreen: draw off-screen, Blt to the primary surface",
    "Fullscreen: lock and write the primary surface directly",
    "Windowed: draw off-screen, Blt to the window",
};

static const char* PRESENT_SHORT[PRESENT_COUNT] = { "flip", "blt", "lock", "windowed" };

static const char* PRESENT_DESC[PRESENT_COUNT] = {
    "page flipping", "Blt to primary", "writing the primary", "windowed",
};

static const int BPP_VALUES[] = { 8, 16, 24, 32 };
#define BPP_COUNT ((int)(sizeof(BPP_VALUES) / sizeof(BPP_VALUES[0])))

#define MAX_DIMENSION 8192

typedef struct Settings
{
    int width, height, bpp, refresh, fps;
    int present, pattern;
    BOOL info, mouse, image_fit;
    char image_path[MAX_PATH];
} Settings;

static void settings_default(Settings* s)
{
    ZeroMemory(s, sizeof(*s));
    s->width = 640;
    s->height = 480;
    s->bpp = 8;
    s->fps = 60;
    s->present = PRESENT_FLIP;
    s->pattern = PAT_COLOR_BARS;
    s->info = TRUE;
    s->mouse = TRUE;
    s->image_fit = TRUE;
}

/* ---------------------------------------------------------------------- */
/* App state                                                               */

enum
{
    ID_APPLY = IDOK,
    ID_STOP = 1001,
    ID_WIDTH, ID_HEIGHT, ID_BPP, ID_REFRESH, ID_FPS,
    ID_PRESENT, ID_PATTERN, ID_INFO, ID_MOUSE,
    ID_IMAGE, ID_BROWSE, ID_FIT, ID_IMGSIZE,
    ID_MODES, ID_QUERY, ID_STATUS,
};

#define TIMER_STATUS 1
#define MAX_CLICKS 16
#define MAX_MODES 512

typedef struct Mode { int w, h, bpp, hz; } Mode;

static struct
{
    HINSTANCE inst;
    HFONT font;
    HWND panel, display;
    HWND ed_width, ed_height, cb_bpp, ed_refresh, ed_fps;
    HWND cb_present, cb_pattern, chk_info, chk_mouse;
    HWND ed_image, chk_fit, cb_modes, st_status;

    char exe_dir[MAX_PATH];
    char ini_path[MAX_PATH];

    HMODULE ddraw_mod;
    char ddraw_path[MAX_PATH + 32];
    HRESULT (WINAPI* DirectDrawCreateEx)(GUID*, LPVOID*, REFIID, IUnknown*);

    LPDIRECTDRAW7 dd;
    BOOL dd_fullscreen;
    LPDIRECTDRAWSURFACE7 primary, back, offscreen;
    LPDIRECTDRAWPALETTE pal;
    BOOL pal_dirty;
    LPDIRECTDRAWCLIPPER clipper;
    DDPIXELFORMAT pf;

    BOOL running;
    Settings cur;
    Canvas canvas;
    uint32_t base_pal[256];

    uint32_t* image;
    int image_w, image_h;
    char image_loaded_path[MAX_PATH];

    uint32_t frame;
    LARGE_INTEGER qpf, fps_t0, next_frame;
    int fps_frames;
    double fps;

    POINT clicks[MAX_CLICKS];
    int click_count, click_next;

    Mode modes[MAX_MODES];
    int mode_count;

    uint32_t quit_after_frames;
    char log_path[MAX_PATH];

    char error[512];
} g;

static void update_status(void);

static void set_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(g.error, sizeof(g.error) - 1, fmt, ap);
    g.error[sizeof(g.error) - 1] = '\0';
    va_end(ap);
}

static const char* dderr_name(HRESULT hr)
{
    static char buf[32];

    switch (hr)
    {
    case DD_OK: return "DD_OK";
    case DDERR_ALREADYINITIALIZED: return "DDERR_ALREADYINITIALIZED";
    case DDERR_CANTCREATEDC: return "DDERR_CANTCREATEDC";
    case DDERR_EXCLUSIVEMODEALREADYSET: return "DDERR_EXCLUSIVEMODEALREADYSET";
    case DDERR_GENERIC: return "DDERR_GENERIC";
    case DDERR_HWNDALREADYSET: return "DDERR_HWNDALREADYSET";
    case DDERR_INCOMPATIBLEPRIMARY: return "DDERR_INCOMPATIBLEPRIMARY";
    case DDERR_INVALIDCAPS: return "DDERR_INVALIDCAPS";
    case DDERR_INVALIDMODE: return "DDERR_INVALIDMODE";
    case DDERR_INVALIDOBJECT: return "DDERR_INVALIDOBJECT";
    case DDERR_INVALIDPARAMS: return "DDERR_INVALIDPARAMS";
    case DDERR_INVALIDPIXELFORMAT: return "DDERR_INVALIDPIXELFORMAT";
    case DDERR_INVALIDRECT: return "DDERR_INVALIDRECT";
    case DDERR_LOCKEDSURFACES: return "DDERR_LOCKEDSURFACES";
    case DDERR_NOBLTHW: return "DDERR_NOBLTHW";
    case DDERR_NOCOOPERATIVELEVELSET: return "DDERR_NOCOOPERATIVELEVELSET";
    case DDERR_NODIRECTDRAWHW: return "DDERR_NODIRECTDRAWHW";
    case DDERR_NOEXCLUSIVEMODE: return "DDERR_NOEXCLUSIVEMODE";
    case DDERR_NOFLIPHW: return "DDERR_NOFLIPHW";
    case DDERR_NOPALETTEATTACHED: return "DDERR_NOPALETTEATTACHED";
    case DDERR_NOTFLIPPABLE: return "DDERR_NOTFLIPPABLE";
    case DDERR_OUTOFMEMORY: return "DDERR_OUTOFMEMORY";
    case DDERR_OUTOFVIDEOMEMORY: return "DDERR_OUTOFVIDEOMEMORY";
    case DDERR_PRIMARYSURFACEALREADYEXISTS: return "DDERR_PRIMARYSURFACEALREADYEXISTS";
    case DDERR_SURFACEBUSY: return "DDERR_SURFACEBUSY";
    case DDERR_SURFACELOST: return "DDERR_SURFACELOST";
    case DDERR_UNSUPPORTED: return "DDERR_UNSUPPORTED";
    case DDERR_UNSUPPORTEDMODE: return "DDERR_UNSUPPORTEDMODE";
    case DDERR_WASSTILLDRAWING: return "DDERR_WASSTILLDRAWING";
    case DDERR_WRONGMODE: return "DDERR_WRONGMODE";
    case E_NOINTERFACE: return "E_NOINTERFACE";
    default:
        _snprintf(buf, sizeof(buf) - 1, "0x%08lX", (unsigned long)hr);
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }
}

static BOOL dd_ok(HRESULT hr, const char* what)
{
    if (SUCCEEDED(hr))
        return TRUE;

    set_error("%s failed: %s", what, dderr_name(hr));
    return FALSE;
}

/* ---------------------------------------------------------------------- */
/* ddraw.dll                                                               */

static BOOL load_ddraw(void)
{
    if (g.ddraw_mod)
        return g.DirectDrawCreateEx != NULL;

    /* No path: Windows looks next to the exe first, then in the system folder */
    g.ddraw_mod = LoadLibraryA("ddraw.dll");
    if (!g.ddraw_mod)
    {
        set_error("Couldn't load ddraw.dll (error %lu)", GetLastError());
        return FALSE;
    }

    char path[MAX_PATH] = "";
    GetModuleFileNameA(g.ddraw_mod, path, sizeof(path));

    char dir[MAX_PATH];
    strcpy(dir, path);
    char* slash = strrchr(dir, '\\');
    if (slash)
        slash[1] = '\0';

    _snprintf(g.ddraw_path, sizeof(g.ddraw_path) - 1, "%s%s", path,
              _stricmp(dir, g.exe_dir) == 0 ? "  (next to ddtest.exe)" : "");
    g.ddraw_path[sizeof(g.ddraw_path) - 1] = '\0';

    g.DirectDrawCreateEx = (HRESULT (WINAPI*)(GUID*, LPVOID*, REFIID, IUnknown*))
        GetProcAddress(g.ddraw_mod, "DirectDrawCreateEx");

    if (!g.DirectDrawCreateEx)
    {
        set_error("This ddraw.dll has no DirectDrawCreateEx");
        return FALSE;
    }

    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Canvas and pixel conversion                                             */

static BOOL canvas_alloc(int w, int h)
{
    if (g.canvas.px && g.canvas.w == w && g.canvas.h == h)
        return TRUE;

    free(g.canvas.px);
    free(g.canvas.idx);
    g.canvas.px = malloc((size_t)w * h * sizeof(uint32_t));
    g.canvas.idx = malloc((size_t)w * h);
    g.canvas.w = w;
    g.canvas.h = h;
    g.canvas.use_idx = FALSE;

    if (!g.canvas.px || !g.canvas.idx)
    {
        free(g.canvas.px);
        free(g.canvas.idx);
        ZeroMemory(&g.canvas, sizeof(g.canvas));
        return FALSE;
    }

    return TRUE;
}

static int mask_shift(DWORD m)
{
    int s = 0;
    if (!m)
        return 0;
    while (!(m & 1)) { m >>= 1; s++; }
    return s;
}

static int mask_bits(DWORD m)
{
    int b = 0;
    m >>= mask_shift(m);
    while (m & 1) { m >>= 1; b++; }
    return b > 8 ? 8 : b;
}

static void convert_to_surface(const Canvas* c, BYTE* dst, LONG pitch, DWORD sw, DWORD sh)
{
    int w = c->w < (int)sw ? c->w : (int)sw;
    int h = c->h < (int)sh ? c->h : (int)sh;
    const DDPIXELFORMAT* pf = &g.pf;
    int bits = (int)pf->dwRGBBitCount;

    if (pf->dwFlags & DDPF_PALETTEINDEXED8)
    {
        for (int y = 0; y < h; y++)
        {
            BYTE* d = dst + (size_t)y * pitch;

            if (c->use_idx)
            {
                memcpy(d, &c->idx[y * c->w], (size_t)w);
            }
            else
            {
                const uint32_t* s = &c->px[y * c->w];
                for (int x = 0; x < w; x++)
                    d[x] = palette_lookup(s[x]);
            }
        }
        return;
    }

    if (bits == 32 && pf->dwRBitMask == 0xFF0000 && pf->dwGBitMask == 0xFF00 && pf->dwBBitMask == 0xFF)
    {
        for (int y = 0; y < h; y++)
            memcpy(dst + (size_t)y * pitch, &c->px[y * c->w], (size_t)w * 4);
        return;
    }

    int rs = mask_shift(pf->dwRBitMask), rb = mask_bits(pf->dwRBitMask);
    int gs = mask_shift(pf->dwGBitMask), gb = mask_bits(pf->dwGBitMask);
    int bs = mask_shift(pf->dwBBitMask), bb = mask_bits(pf->dwBBitMask);

    for (int y = 0; y < h; y++)
    {
        BYTE* d = dst + (size_t)y * pitch;
        const uint32_t* s = &c->px[y * c->w];

        for (int x = 0; x < w; x++)
        {
            uint32_t p = s[x];
            uint32_t v = ((COL_R(p) >> (8 - rb)) << rs) |
                         ((COL_G(p) >> (8 - gb)) << gs) |
                         ((COL_B(p) >> (8 - bb)) << bs);

            switch (bits)
            {
            case 16: ((uint16_t*)d)[x] = (uint16_t)v; break;
            case 24: d[x * 3] = (BYTE)v; d[x * 3 + 1] = (BYTE)(v >> 8); d[x * 3 + 2] = (BYTE)(v >> 16); break;
            case 32: ((uint32_t*)d)[x] = v; break;
            default: break;
            }
        }
    }
}

/* ---------------------------------------------------------------------- */
/* DirectDraw setup and teardown                                           */

static void release_surfaces(void)
{
    if (g.offscreen) { IDirectDrawSurface7_Release(g.offscreen); g.offscreen = NULL; }
    if (g.back) { IDirectDrawSurface7_Release(g.back); g.back = NULL; }
    if (g.primary) { IDirectDrawSurface7_Release(g.primary); g.primary = NULL; }
    if (g.clipper) { IDirectDrawClipper_Release(g.clipper); g.clipper = NULL; }
    if (g.pal) { IDirectDrawPalette_Release(g.pal); g.pal = NULL; }
    g.pal_dirty = FALSE;
}

static void release_dd(void)
{
    release_surfaces();

    if (g.dd)
    {
        if (g.dd_fullscreen)
            IDirectDraw7_RestoreDisplayMode(g.dd);

        IDirectDraw7_SetCooperativeLevel(g.dd, g.display, DDSCL_NORMAL);
        IDirectDraw7_Release(g.dd);
        g.dd = NULL;
    }
}

static LPDIRECTDRAWSURFACE7 draw_target(void)
{
    switch (g.cur.present)
    {
    case PRESENT_FLIP: return g.back;
    case PRESENT_LOCK: return g.primary;
    default: return g.offscreen;
    }
}

static void pf_for_bpp(int bpp, DDPIXELFORMAT* pf)
{
    ZeroMemory(pf, sizeof(*pf));
    pf->dwSize = sizeof(*pf);
    pf->dwFlags = DDPF_RGB;
    pf->dwRGBBitCount = (DWORD)bpp;

    switch (bpp)
    {
    case 8:
        pf->dwFlags |= DDPF_PALETTEINDEXED8;
        break;
    case 16:
        pf->dwRBitMask = 0xF800;
        pf->dwGBitMask = 0x07E0;
        pf->dwBBitMask = 0x001F;
        break;
    default:
        pf->dwRBitMask = 0xFF0000;
        pf->dwGBitMask = 0x00FF00;
        pf->dwBBitMask = 0x0000FF;
        break;
    }
}

static void setup_window(const Settings* s, BOOL fullscreen)
{
    char title[128];
    _snprintf(title, sizeof(title) - 1, "ddtest - %d x %d, %d-bit", s->width, s->height, s->bpp);
    title[sizeof(title) - 1] = '\0';
    SetWindowTextA(g.display, title);

    if (fullscreen)
    {
        SetWindowLongA(g.display, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g.display, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else
    {
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT r = { 0, 0, s->width, s->height };
        AdjustWindowRectEx(&r, style, FALSE, 0);

        RECT work;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);

        int ww = r.right - r.left, wh = r.bottom - r.top;
        int x = work.left + ((work.right - work.left) - ww) / 2;
        int y = work.top + ((work.bottom - work.top) - wh) / 2;
        if (x < work.left) x = work.left;
        if (y < work.top) y = work.top;

        SetWindowLongA(g.display, GWL_STYLE, style | WS_VISIBLE);
        SetWindowPos(g.display, HWND_TOP, x, y, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    SetForegroundWindow(g.display);
    SetFocus(g.display);
}

static BOOL create_surfaces(const Settings* s)
{
    DDSURFACEDESC2 sd;
    HRESULT hr;

    ZeroMemory(&sd, sizeof(sd));
    sd.dwSize = sizeof(sd);

    if (s->present == PRESENT_FLIP)
    {
        sd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
        sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
        sd.dwBackBufferCount = 1;
    }
    else
    {
        sd.dwFlags = DDSD_CAPS;
        sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    }

    hr = IDirectDraw7_CreateSurface(g.dd, &sd, &g.primary, NULL);
    if (!dd_ok(hr, "CreateSurface (primary)"))
        return FALSE;

    if (s->present == PRESENT_FLIP)
    {
        DDSCAPS2 caps;
        ZeroMemory(&caps, sizeof(caps));
        caps.dwCaps = DDSCAPS_BACKBUFFER;

        hr = IDirectDrawSurface7_GetAttachedSurface(g.primary, &caps, &g.back);
        if (!dd_ok(hr, "GetAttachedSurface (back buffer)"))
            return FALSE;
    }

    if (s->present == PRESENT_WINDOWED)
    {
        hr = IDirectDraw7_CreateClipper(g.dd, 0, &g.clipper, NULL);
        if (!dd_ok(hr, "CreateClipper"))
            return FALSE;

        hr = IDirectDrawClipper_SetHWnd(g.clipper, 0, g.display);
        if (!dd_ok(hr, "IDirectDrawClipper::SetHWnd"))
            return FALSE;

        hr = IDirectDrawSurface7_SetClipper(g.primary, g.clipper);
        if (!dd_ok(hr, "SetClipper"))
            return FALSE;
    }

    if (s->present == PRESENT_BLT || s->present == PRESENT_WINDOWED)
    {
        DDPIXELFORMAT primary_pf;
        ZeroMemory(&primary_pf, sizeof(primary_pf));
        primary_pf.dwSize = sizeof(primary_pf);
        IDirectDrawSurface7_GetPixelFormat(g.primary, &primary_pf);

        ZeroMemory(&sd, sizeof(sd));
        sd.dwSize = sizeof(sd);
        sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
        sd.dwWidth = (DWORD)s->width;
        sd.dwHeight = (DWORD)s->height;

        /* Windowed: the desktop decides the primary's format, so ask for the
           chosen bit depth explicitly if it differs */
        if (s->present == PRESENT_WINDOWED && (int)primary_pf.dwRGBBitCount != s->bpp)
        {
            sd.dwFlags |= DDSD_PIXELFORMAT;
            pf_for_bpp(s->bpp, &sd.ddpfPixelFormat);
        }

        hr = IDirectDraw7_CreateSurface(g.dd, &sd, &g.offscreen, NULL);
        if (!dd_ok(hr, "CreateSurface (off-screen)"))
            return FALSE;
    }

    g.cur.present = s->present; /* draw_target() needs it */

    ZeroMemory(&g.pf, sizeof(g.pf));
    g.pf.dwSize = sizeof(g.pf);
    hr = IDirectDrawSurface7_GetPixelFormat(draw_target(), &g.pf);
    if (!dd_ok(hr, "GetPixelFormat"))
        return FALSE;

    if (g.pf.dwFlags & DDPF_PALETTEINDEXED8)
    {
        PALETTEENTRY pe[256];
        for (int i = 0; i < 256; i++)
        {
            pe[i].peRed = (BYTE)COL_R(g.base_pal[i]);
            pe[i].peGreen = (BYTE)COL_G(g.base_pal[i]);
            pe[i].peBlue = (BYTE)COL_B(g.base_pal[i]);
            pe[i].peFlags = 0;
        }

        hr = IDirectDraw7_CreatePalette(g.dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, pe, &g.pal, NULL);
        if (!dd_ok(hr, "CreatePalette"))
            return FALSE;

        hr = IDirectDrawSurface7_SetPalette(s->present == PRESENT_WINDOWED ? g.offscreen : g.primary, g.pal);
        if (!dd_ok(hr, "SetPalette"))
            return FALSE;
    }

    return TRUE;
}

static BOOL start(const Settings* s)
{
    BOOL fullscreen = s->present != PRESENT_WINDOWED;
    HRESULT hr;

    if (!load_ddraw())
        return FALSE;

    release_surfaces();

    if (g.dd && g.dd_fullscreen != fullscreen)
        release_dd();

    setup_window(s, fullscreen);

    if (!g.dd)
    {
        hr = g.DirectDrawCreateEx(NULL, (LPVOID*)&g.dd, &IID_IDirectDraw7, NULL);
        if (!dd_ok(hr, "DirectDrawCreateEx"))
            return FALSE;

        hr = IDirectDraw7_SetCooperativeLevel(g.dd, g.display,
            fullscreen ? (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT) : DDSCL_NORMAL);
        if (!dd_ok(hr, "SetCooperativeLevel"))
            return FALSE;

        g.dd_fullscreen = fullscreen;
    }

    if (fullscreen)
    {
        hr = IDirectDraw7_SetDisplayMode(g.dd, (DWORD)s->width, (DWORD)s->height, (DWORD)s->bpp, (DWORD)s->refresh, 0);
        if (!dd_ok(hr, "SetDisplayMode"))
            return FALSE;

        /* Like most games: cover the screen, which is now the new mode's size */
        SetWindowPos(g.display, HWND_TOP, 0, 0, s->width, s->height, SWP_SHOWWINDOW);
    }

    if (!create_surfaces(s))
        return FALSE;

    if (!canvas_alloc(s->width, s->height))
    {
        set_error("Not enough memory for a %d x %d image", s->width, s->height);
        return FALSE;
    }

    g.frame = 0;
    g.fps = 0.0;
    g.fps_frames = 0;
    g.click_count = 0;
    g.click_next = 0;
    g.next_frame.QuadPart = 0;
    QueryPerformanceCounter(&g.fps_t0);
    return TRUE;
}

static void stop(void)
{
    release_dd();
    g.running = FALSE;

    if (g.display)
        ShowWindow(g.display, SW_HIDE);

    if (g.panel && IsWindow(g.panel))
        SetForegroundWindow(g.panel);

    update_status();
}

/* ---------------------------------------------------------------------- */
/* Drawing a frame                                                         */

static void draw_info(Canvas* c)
{
    int ts = cv_text_size(c) * 3 / 4;
    if (ts < 8)
        ts = 8;

    char line[MAX_PATH + 96];
    int y = 2;

    _snprintf(line, sizeof(line) - 1, "%d x %d, %d-bit, %s", g.cur.width, g.cur.height, g.cur.bpp,
              PRESENT_DESC[g.cur.present]);
    line[sizeof(line) - 1] = '\0';
    y += cv_label(c, 3, y, ts, WHITE, BLACK, line);

    _snprintf(line, sizeof(line) - 1, "%s - frame %u - %.1f fps", pattern_name(g.cur.pattern), g.frame, g.fps);
    line[sizeof(line) - 1] = '\0';
    y += cv_label(c, 3, y, ts, WHITE, BLACK, line);

    _snprintf(line, sizeof(line) - 1, "ddraw.dll: %s", g.ddraw_path);
    line[sizeof(line) - 1] = '\0';
    y += cv_label(c, 3, y, ts, WHITE, BLACK, line);

    cv_label(c, 3, y, ts, COL(160, 160, 160), BLACK, "Esc: stop  Left/Right: pattern  I: info  M: mouse");
}

static void draw_mouse(Canvas* c)
{
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(g.display, &p);

    int len = c->h / 20 > 6 ? c->h / 20 : 6;
    int ts = cv_text_size(c) * 3 / 4;
    if (ts < 8)
        ts = 8;

    for (int i = 0; i < g.click_count; i++)
    {
        int r = c->h / 80 > 3 ? c->h / 80 : 3;
        cv_ellipse(c, g.clicks[i].x, g.clicks[i].y, r, r, COL(160, 160, 160), 1);
        cv_put(c, g.clicks[i].x, g.clicks[i].y, COL(160, 160, 160));
    }

    /* White crosshair with a black outline, visible on any background */
    for (int o = -1; o <= 1; o += 2)
    {
        cv_hline(c, p.x - len, p.x + len, p.y + o, BLACK);
        cv_vline(c, p.x + o, p.y - len, p.y + len, BLACK);
    }
    cv_hline(c, p.x - len, p.x + len, p.y, WHITE);
    cv_vline(c, p.x, p.y - len, p.y + len, WHITE);

    char text[32];
    _snprintf(text, sizeof(text) - 1, "%ld, %ld", (long)p.x, (long)p.y);
    text[sizeof(text) - 1] = '\0';
    cv_label(c, p.x + 5, p.y + 5, ts, WHITE, BLACK, text);
}

static void restore_surfaces(void)
{
    if (FAILED(IDirectDraw7_RestoreAllSurfaces(g.dd)))
        Sleep(50); /* e.g. while alt-tabbed out of exclusive fullscreen */
}

static void render_frame(void)
{
    if (IsIconic(g.display))
    {
        Sleep(50);
        return;
    }

    PatternContext ctx;
    ZeroMemory(&ctx, sizeof(ctx));
    ctx.frame = g.frame;
    ctx.palette = g.base_pal;
    ctx.hw_palette = (g.pf.dwFlags & DDPF_PALETTEINDEXED8) != 0;
    ctx.image = g.image;
    ctx.image_w = g.image_w;
    ctx.image_h = g.image_h;
    ctx.image_fit = g.cur.image_fit;

    pattern_draw(g.cur.pattern, &g.canvas, &ctx);

    if (g.cur.mouse)
        draw_mouse(&g.canvas);

    if (g.cur.info)
        draw_info(&g.canvas);

    if (g.pal)
    {
        if (g.cur.pattern == PAT_PALETTE)
        {
            PALETTEENTRY pe[PAL_CYCLE_COUNT];
            for (int i = 0; i < PAL_CYCLE_COUNT; i++)
            {
                uint32_t col = g.base_pal[(i + g.frame) % PAL_CYCLE_COUNT];
                pe[i].peRed = (BYTE)COL_R(col);
                pe[i].peGreen = (BYTE)COL_G(col);
                pe[i].peBlue = (BYTE)COL_B(col);
                pe[i].peFlags = 0;
            }
            IDirectDrawPalette_SetEntries(g.pal, 0, 0, PAL_CYCLE_COUNT, pe);
            g.pal_dirty = TRUE;
        }
        else if (g.pal_dirty)
        {
            PALETTEENTRY pe[PAL_CYCLE_COUNT];
            for (int i = 0; i < PAL_CYCLE_COUNT; i++)
            {
                pe[i].peRed = (BYTE)COL_R(g.base_pal[i]);
                pe[i].peGreen = (BYTE)COL_G(g.base_pal[i]);
                pe[i].peBlue = (BYTE)COL_B(g.base_pal[i]);
                pe[i].peFlags = 0;
            }
            IDirectDrawPalette_SetEntries(g.pal, 0, 0, PAL_CYCLE_COUNT, pe);
            g.pal_dirty = FALSE;
        }
    }

    LPDIRECTDRAWSURFACE7 target = draw_target();
    DDSURFACEDESC2 sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.dwSize = sizeof(sd);

    HRESULT hr = IDirectDrawSurface7_Lock(target, NULL, &sd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    if (hr == DDERR_SURFACELOST)
    {
        restore_surfaces();
        return;
    }
    if (!dd_ok(hr, "Lock"))
    {
        stop();
        return;
    }

    convert_to_surface(&g.canvas, (BYTE*)sd.lpSurface, sd.lPitch, sd.dwWidth, sd.dwHeight);
    IDirectDrawSurface7_Unlock(target, NULL);

    switch (g.cur.present)
    {
    case PRESENT_FLIP:
        hr = IDirectDrawSurface7_Flip(g.primary, NULL, DDFLIP_WAIT);
        break;

    case PRESENT_BLT:
        hr = IDirectDrawSurface7_Blt(g.primary, NULL, g.offscreen, NULL, DDBLT_WAIT, NULL);
        break;

    case PRESENT_WINDOWED:
    {
        RECT r;
        POINT origin = { 0, 0 };
        GetClientRect(g.display, &r);
        ClientToScreen(g.display, &origin);
        OffsetRect(&r, origin.x, origin.y);
        hr = IDirectDrawSurface7_Blt(g.primary, &r, g.offscreen, NULL, DDBLT_WAIT, NULL);
        break;
    }

    default:
        hr = DD_OK;
        break;
    }

    if (hr == DDERR_SURFACELOST)
    {
        restore_surfaces();
    }
    else if (hr != DDERR_WASSTILLDRAWING && !dd_ok(hr, g.cur.present == PRESENT_FLIP ? "Flip" : "Blt"))
    {
        stop();
        return;
    }

    g.frame++;
    g.fps_frames++;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = (double)(now.QuadPart - g.fps_t0.QuadPart) / (double)g.qpf.QuadPart;
    if (elapsed >= 0.5)
    {
        g.fps = g.fps_frames / elapsed;
        g.fps_frames = 0;
        g.fps_t0 = now;
    }
}

static void wait_for_next_frame(void)
{
    if (g.cur.fps <= 0)
        return;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    LONGLONG period = g.qpf.QuadPart / g.cur.fps;

    if (g.next_frame.QuadPart == 0 || now.QuadPart > g.next_frame.QuadPart + period)
        g.next_frame.QuadPart = now.QuadPart + period; /* first frame, or we fell behind */
    else
        g.next_frame.QuadPart += period;

    while (now.QuadPart < g.next_frame.QuadPart)
    {
        LONGLONG ms = (g.next_frame.QuadPart - now.QuadPart) * 1000 / g.qpf.QuadPart;
        Sleep(ms > 1 ? (DWORD)(ms - 1) : 0);
        QueryPerformanceCounter(&now);
    }
}

/* ---------------------------------------------------------------------- */
/* Settings file and command line                                          */

static void settings_load(Settings* s)
{
    const char* sec = "ddtest";
    s->width = GetPrivateProfileIntA(sec, "width", s->width, g.ini_path);
    s->height = GetPrivateProfileIntA(sec, "height", s->height, g.ini_path);
    s->bpp = GetPrivateProfileIntA(sec, "bpp", s->bpp, g.ini_path);
    s->refresh = GetPrivateProfileIntA(sec, "refresh", s->refresh, g.ini_path);
    s->fps = GetPrivateProfileIntA(sec, "fps", s->fps, g.ini_path);
    s->present = GetPrivateProfileIntA(sec, "present", s->present, g.ini_path);
    s->pattern = GetPrivateProfileIntA(sec, "pattern", s->pattern, g.ini_path);
    s->info = GetPrivateProfileIntA(sec, "info", s->info, g.ini_path) != 0;
    s->mouse = GetPrivateProfileIntA(sec, "mouse", s->mouse, g.ini_path) != 0;
    s->image_fit = GetPrivateProfileIntA(sec, "image_fit", s->image_fit, g.ini_path) != 0;
    GetPrivateProfileStringA(sec, "image", "", s->image_path, sizeof(s->image_path), g.ini_path);

    if (s->present < 0 || s->present >= PRESENT_COUNT) s->present = PRESENT_FLIP;
    if (s->pattern < 0 || s->pattern >= PAT_COUNT) s->pattern = PAT_COLOR_BARS;
}

static void write_int(const char* key, int value)
{
    char buf[16];
    _snprintf(buf, sizeof(buf) - 1, "%d", value);
    buf[sizeof(buf) - 1] = '\0';
    WritePrivateProfileStringA("ddtest", key, buf, g.ini_path);
}

static void settings_save(const Settings* s)
{
    write_int("width", s->width);
    write_int("height", s->height);
    write_int("bpp", s->bpp);
    write_int("refresh", s->refresh);
    write_int("fps", s->fps);
    write_int("present", s->present);
    write_int("pattern", s->pattern);
    write_int("info", s->info);
    write_int("mouse", s->mouse);
    write_int("image_fit", s->image_fit);
    WritePrivateProfileStringA("ddtest", "image", s->image_path, g.ini_path);
}

static const char* USAGE =
    "ddtest options (all optional):\n\n"
    "  --size WxH, --width N, --height N\n"
    "  --bpp 8|16|24|32\n"
    "  --refresh HZ        (0 = default)\n"
    "  --fps N             frame rate limit (0 = none)\n"
    "  --present flip|blt|lock|windowed\n"
    "  --pattern NAME|NUMBER\n"
    "      bars, ramps, grid, pixels, geometry, dither, text,\n"
    "      motion, interlaced, palette, white, image\n"
    "  --image FILE, --fit\n"
    "  --start             apply the settings straight away\n"
    "  --dump DIR          save the patterns as PNG files and exit\n"
    "  --frame N           animation frame for --dump\n"
    "  --frames N          with --start: exit after N frames\n"
    "  --log FILE          with --frames: write the result to FILE\n";

static BOOL parse_args(Settings* s, BOOL* start_now, char* dump_dir, uint32_t* frame, int* only_pattern)
{
    for (int i = 1; i < __argc; i++)
    {
        const char* a = __argv[i];
        const char* v = (i + 1 < __argc) ? __argv[i + 1] : NULL;
        BOOL takes_value = TRUE;

        if (_stricmp(a, "--start") == 0) { *start_now = TRUE; takes_value = FALSE; }
        else if (_stricmp(a, "--fit") == 0) { s->image_fit = TRUE; takes_value = FALSE; }
        else if (_stricmp(a, "--help") == 0 || _stricmp(a, "/?") == 0)
        {
            MessageBoxA(NULL, USAGE, "ddtest", MB_OK | MB_ICONINFORMATION);
            return FALSE;
        }
        else if (!v)
        {
            char msg[256];
            _snprintf(msg, sizeof(msg) - 1, "Unknown option or missing value: %s\n\n", a);
            msg[sizeof(msg) - 1] = '\0';
            char full[1024];
            _snprintf(full, sizeof(full) - 1, "%s%s", msg, USAGE);
            full[sizeof(full) - 1] = '\0';
            MessageBoxA(NULL, full, "ddtest", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        else if (_stricmp(a, "--size") == 0) { sscanf(v, "%dx%d", &s->width, &s->height); }
        else if (_stricmp(a, "--width") == 0) { s->width = atoi(v); }
        else if (_stricmp(a, "--height") == 0) { s->height = atoi(v); }
        else if (_stricmp(a, "--bpp") == 0) { s->bpp = atoi(v); }
        else if (_stricmp(a, "--refresh") == 0) { s->refresh = atoi(v); }
        else if (_stricmp(a, "--fps") == 0) { s->fps = atoi(v); }
        else if (_stricmp(a, "--image") == 0) { strncpy(s->image_path, v, MAX_PATH - 1); s->image_path[MAX_PATH - 1] = '\0'; }
        else if (_stricmp(a, "--dump") == 0) { strncpy(dump_dir, v, MAX_PATH - 1); dump_dir[MAX_PATH - 1] = '\0'; }
        else if (_stricmp(a, "--frame") == 0) { *frame = (uint32_t)strtoul(v, NULL, 10); }
        else if (_stricmp(a, "--frames") == 0) { g.quit_after_frames = (uint32_t)strtoul(v, NULL, 10); }
        else if (_stricmp(a, "--log") == 0) { strncpy(g.log_path, v, MAX_PATH - 1); g.log_path[MAX_PATH - 1] = '\0'; }
        else if (_stricmp(a, "--present") == 0)
        {
            int p = -1;
            for (int k = 0; k < PRESENT_COUNT; k++)
                if (_stricmp(v, PRESENT_SHORT[k]) == 0)
                    p = k;
            if (p < 0)
            {
                MessageBoxA(NULL, USAGE, "ddtest: unknown --present value", MB_OK | MB_ICONERROR);
                return FALSE;
            }
            s->present = p;
        }
        else if (_stricmp(a, "--pattern") == 0)
        {
            int p = pattern_from_short_name(v);
            if (p < 0 && v[0] >= '0' && v[0] <= '9')
                p = atoi(v);
            if (p < 0 || p >= PAT_COUNT)
            {
                MessageBoxA(NULL, USAGE, "ddtest: unknown --pattern value", MB_OK | MB_ICONERROR);
                return FALSE;
            }
            s->pattern = p;
            *only_pattern = p;
        }
        else
        {
            char full[1024];
            _snprintf(full, sizeof(full) - 1, "Unknown option: %s\n\n%s", a, USAGE);
            full[sizeof(full) - 1] = '\0';
            MessageBoxA(NULL, full, "ddtest", MB_OK | MB_ICONERROR);
            return FALSE;
        }

        if (takes_value)
            i++;
    }

    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Image file                                                              */

static BOOL load_image_file(const char* path)
{
    free(g.image);
    g.image = NULL;
    g.image_w = g.image_h = 0;
    g.image_loaded_path[0] = '\0';

    if (!path[0])
        return TRUE;

    char err[256];
    if (!image_load(path, &g.image, &g.image_w, &g.image_h, err, sizeof(err)))
    {
        set_error("%s: %s", err, path);
        return FALSE;
    }

    strcpy(g.image_loaded_path, path);
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* --dump: save patterns as PNG files                                      */

static int run_dump(const Settings* s, const char* dir, uint32_t frame, int only_pattern)
{
    if (!canvas_alloc(s->width, s->height))
        return 1;

    CreateDirectoryA(dir, NULL);

    if (s->image_path[0])
        load_image_file(s->image_path);

    uint32_t* out = malloc((size_t)s->width * s->height * sizeof(uint32_t));
    if (!out)
        return 1;

    int failures = 0;

    for (int p = 0; p < PAT_COUNT; p++)
    {
        if (only_pattern >= 0 && p != only_pattern)
            continue;

        PatternContext ctx;
        ZeroMemory(&ctx, sizeof(ctx));
        ctx.frame = frame;
        ctx.palette = g.base_pal;
        ctx.hw_palette = s->bpp == 8;
        ctx.image = g.image;
        ctx.image_w = g.image_w;
        ctx.image_h = g.image_h;
        ctx.image_fit = s->image_fit;

        Canvas* c = &g.canvas;
        pattern_draw(p, c, &ctx);

        /* Show what the chosen bit depth can actually display */
        for (int i = 0; i < c->w * c->h; i++)
        {
            uint32_t px = c->px[i];

            if (s->bpp == 8)
            {
                int index = c->use_idx ? c->idx[i] : palette_lookup(px);
                if (c->use_idx && index < PAL_CYCLE_COUNT)
                    index = (int)((index + frame) % PAL_CYCLE_COUNT);
                out[i] = g.base_pal[index];
            }
            else if (s->bpp == 16)
            {
                int r = COL_R(px) & 0xF8, gr = COL_G(px) & 0xFC, b = COL_B(px) & 0xF8;
                out[i] = COL(r | (r >> 5), gr | (gr >> 6), b | (b >> 5));
            }
            else
            {
                out[i] = px;
            }
        }

        char path[MAX_PATH];
        _snprintf(path, sizeof(path) - 1, "%s\\%s-%dx%d-%d.png", dir, pattern_short_name(p),
                  s->width, s->height, s->bpp);
        path[sizeof(path) - 1] = '\0';

        char err[256];
        if (!image_save_png(path, out, c->w, c->h, err, sizeof(err)))
            failures++;
    }

    free(out);
    return failures ? 1 : 0;
}

/* ---------------------------------------------------------------------- */
/* Control panel                                                           */

static HWND add_control(const char* cls, const char* text, DWORD style, int x, int y, int w, int h, int id)
{
    DWORD ex = (strcmp(cls, "EDIT") == 0) ? WS_EX_CLIENTEDGE : 0;
    HWND hwnd = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                g.panel, (HMENU)(INT_PTR)id, g.inst, NULL);
    SendMessageA(hwnd, WM_SETFONT, (WPARAM)g.font, TRUE);
    return hwnd;
}

static HWND add_label(const char* text, int x, int y, int w)
{
    return add_control("STATIC", text, SS_LEFT | SS_NOPREFIX, x, y + 3, w, 18, -1);
}

#define PANEL_W 500
#define PANEL_H 400

static void create_controls(void)
{
    const DWORD edit = WS_TABSTOP | ES_AUTOHSCROLL;
    const DWORD number = edit | ES_NUMBER;
    const DWORD combo = WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST;
    const DWORD check = WS_TABSTOP | BS_AUTOCHECKBOX;
    const DWORD button = WS_TABSTOP | BS_PUSHBUTTON;

    int y = 12;
    add_label("Width:", 12, y, 44);
    g.ed_width = add_control("EDIT", "", number, 58, y, 58, 22, ID_WIDTH);
    add_label("Height:", 128, y, 46);
    g.ed_height = add_control("EDIT", "", number, 176, y, 58, 22, ID_HEIGHT);
    add_label("Bit depth:", 248, y, 60);
    g.cb_bpp = add_control("COMBOBOX", "", combo, 310, y, 70, 200, ID_BPP);
    for (int i = 0; i < BPP_COUNT; i++)
    {
        char buf[8];
        _snprintf(buf, sizeof(buf) - 1, "%d", BPP_VALUES[i]);
        buf[sizeof(buf) - 1] = '\0';
        ComboBox_AddString(g.cb_bpp, buf);
    }

    y += 32;
    add_label("Refresh rate:", 12, y, 78);
    g.ed_refresh = add_control("EDIT", "", number, 92, y, 50, 22, ID_REFRESH);
    add_label("Hz (0 = default)", 148, y, 100);
    add_label("Frame limit:", 260, y, 70);
    g.ed_fps = add_control("EDIT", "", number, 332, y, 50, 22, ID_FPS);
    add_label("fps (0 = none)", 388, y, 100);

    y += 32;
    add_label("Presentation:", 12, y, 78);
    g.cb_present = add_control("COMBOBOX", "", combo, 92, y, 396, 200, ID_PRESENT);
    for (int i = 0; i < PRESENT_COUNT; i++)
        ComboBox_AddString(g.cb_present, PRESENT_NAMES[i]);

    y += 32;
    add_label("Pattern:", 12, y, 78);
    g.cb_pattern = add_control("COMBOBOX", "", combo, 92, y, 220, 300, ID_PATTERN);
    for (int i = 0; i < PAT_COUNT; i++)
        ComboBox_AddString(g.cb_pattern, pattern_name(i));
    g.chk_info = add_control("BUTTON", "Info text", check, 324, y + 2, 76, 20, ID_INFO);
    g.chk_mouse = add_control("BUTTON", "Crosshair", check, 404, y + 2, 84, 20, ID_MOUSE);

    y += 32;
    add_label("Image file:", 12, y, 78);
    g.ed_image = add_control("EDIT", "", edit, 92, y, 312, 22, ID_IMAGE);
    add_control("BUTTON", "Browse...", button, 410, y - 1, 78, 24, ID_BROWSE);

    y += 32;
    g.chk_fit = add_control("BUTTON", "Scale image to fit", check, 92, y + 2, 150, 20, ID_FIT);
    add_control("BUTTON", "Use image size", button, 250, y - 1, 110, 24, ID_IMGSIZE);

    y += 32;
    add_label("Display modes:", 12, y, 80);
    g.cb_modes = add_control("COMBOBOX", "", combo, 92, y, 312, 300, ID_MODES);
    add_control("BUTTON", "Query", button, 410, y - 1, 78, 24, ID_QUERY);

    y += 40;
    add_control("BUTTON", "Apply", WS_TABSTOP | BS_DEFPUSHBUTTON, 92, y, 96, 28, ID_APPLY);
    add_control("BUTTON", "Stop", button, 196, y, 96, 28, ID_STOP);

    y += 38;
    add_control("STATIC",
                "In the test window: Esc stops, Left/Right change the pattern, I and M toggle the overlays,\n"
                "left click marks a point, right click clears the marks.",
                SS_LEFT | SS_NOPREFIX, 12, y, 476, 34, -1);

    y += 40;
    g.st_status = add_control("STATIC", "", SS_LEFT | SS_NOPREFIX | SS_SUNKEN, 12, y, 476, 60, ID_STATUS);
}

static void write_panel(const Settings* s)
{
    char buf[16];

    _snprintf(buf, sizeof(buf) - 1, "%d", s->width); buf[sizeof(buf) - 1] = '\0';
    SetWindowTextA(g.ed_width, buf);
    _snprintf(buf, sizeof(buf) - 1, "%d", s->height); buf[sizeof(buf) - 1] = '\0';
    SetWindowTextA(g.ed_height, buf);
    _snprintf(buf, sizeof(buf) - 1, "%d", s->refresh); buf[sizeof(buf) - 1] = '\0';
    SetWindowTextA(g.ed_refresh, buf);
    _snprintf(buf, sizeof(buf) - 1, "%d", s->fps); buf[sizeof(buf) - 1] = '\0';
    SetWindowTextA(g.ed_fps, buf);

    int bpp_index = 0;
    for (int i = 0; i < BPP_COUNT; i++)
        if (BPP_VALUES[i] == s->bpp)
            bpp_index = i;
    ComboBox_SetCurSel(g.cb_bpp, bpp_index);

    ComboBox_SetCurSel(g.cb_present, s->present);
    ComboBox_SetCurSel(g.cb_pattern, s->pattern);
    Button_SetCheck(g.chk_info, s->info ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(g.chk_mouse, s->mouse ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(g.chk_fit, s->image_fit ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextA(g.ed_image, s->image_path);
}

static BOOL read_int(HWND edit, const char* name, int min, int max, int* out)
{
    char buf[32];
    GetWindowTextA(edit, buf, sizeof(buf));

    char* end;
    long v = strtol(buf, &end, 10);

    if (end == buf || v < min || v > max)
    {
        set_error("%s must be a number from %d to %d", name, min, max);
        return FALSE;
    }

    *out = (int)v;
    return TRUE;
}

static BOOL read_panel(Settings* s)
{
    if (!read_int(g.ed_width, "Width", 1, MAX_DIMENSION, &s->width)) return FALSE;
    if (!read_int(g.ed_height, "Height", 1, MAX_DIMENSION, &s->height)) return FALSE;
    if (!read_int(g.ed_refresh, "Refresh rate", 0, 1000, &s->refresh)) return FALSE;
    if (!read_int(g.ed_fps, "Frame limit", 0, 10000, &s->fps)) return FALSE;

    int bpp_index = ComboBox_GetCurSel(g.cb_bpp);
    s->bpp = BPP_VALUES[bpp_index >= 0 ? bpp_index : 0];

    s->present = ComboBox_GetCurSel(g.cb_present);
    if (s->present < 0) s->present = PRESENT_FLIP;

    s->pattern = ComboBox_GetCurSel(g.cb_pattern);
    if (s->pattern < 0) s->pattern = PAT_COLOR_BARS;

    s->info = Button_GetCheck(g.chk_info) == BST_CHECKED;
    s->mouse = Button_GetCheck(g.chk_mouse) == BST_CHECKED;
    s->image_fit = Button_GetCheck(g.chk_fit) == BST_CHECKED;
    GetWindowTextA(g.ed_image, s->image_path, sizeof(s->image_path));
    return TRUE;
}

static void update_status(void)
{
    if (!g.st_status || !IsWindow(g.st_status))
        return;

    char state[256];
    if (g.running)
        _snprintf(state, sizeof(state) - 1, "Running: %d x %d, %d-bit, %s - %.1f fps",
                  g.cur.width, g.cur.height, g.cur.bpp, PRESENT_DESC[g.cur.present], g.fps);
    else
        _snprintf(state, sizeof(state) - 1, "Stopped");
    state[sizeof(state) - 1] = '\0';

    char text[1200];
    _snprintf(text, sizeof(text) - 1, "%s\r\nddraw.dll: %s\r\n%s%s", state,
              g.ddraw_path[0] ? g.ddraw_path : "(not loaded)",
              g.error[0] ? "Last error: " : "", g.error);
    text[sizeof(text) - 1] = '\0';

    char old[1200];
    GetWindowTextA(g.st_status, old, sizeof(old));
    if (strcmp(old, text) != 0)
        SetWindowTextA(g.st_status, text);
}

static void apply(void)
{
    Settings s;

    g.error[0] = '\0';

    if (!read_panel(&s))
    {
        update_status();
        return;
    }

    if (!g.quit_after_frames) /* don't overwrite the user's settings from scripted runs */
        settings_save(&s);

    if (strcmp(s.image_path, g.image_loaded_path) != 0)
        load_image_file(s.image_path);

    BOOL restart = !g.running ||
        s.width != g.cur.width || s.height != g.cur.height || s.bpp != g.cur.bpp ||
        s.refresh != g.cur.refresh || s.present != g.cur.present;

    if (restart)
    {
        g.cur = s;
        if (!start(&s))
        {
            char err[sizeof(g.error)];
            strcpy(err, g.error);
            stop();
            strcpy(g.error, err);
            update_status();
            return;
        }
    }

    g.cur = s;
    g.running = TRUE;
    SetForegroundWindow(g.display);
    SetFocus(g.display);
    update_status();
}

static void browse_image(void)
{
    char path[MAX_PATH];
    GetWindowTextA(g.ed_image, path, sizeof(path));

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.panel;
    ofn.lpstrFilter = "Images (*.png, *.bmp, *.jpg, *.gif, *.tif)\0*.png;*.bmp;*.jpg;*.jpeg;*.gif;*.tif;*.tiff\0"
                      "All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameA(&ofn))
        SetWindowTextA(g.ed_image, path);
}

static void use_image_size(void)
{
    char path[MAX_PATH];
    GetWindowTextA(g.ed_image, path, sizeof(path));

    g.error[0] = '\0';

    if (!path[0])
        set_error("Choose an image file first");
    else if (load_image_file(path))
    {
        char buf[16];
        _snprintf(buf, sizeof(buf) - 1, "%d", g.image_w); buf[sizeof(buf) - 1] = '\0';
        SetWindowTextA(g.ed_width, buf);
        _snprintf(buf, sizeof(buf) - 1, "%d", g.image_h); buf[sizeof(buf) - 1] = '\0';
        SetWindowTextA(g.ed_height, buf);
    }

    update_status();
}

static HRESULT WINAPI enum_modes_cb(LPDDSURFACEDESC2 sd, LPVOID ctx)
{
    (void)ctx;

    if (g.mode_count < MAX_MODES)
    {
        Mode* m = &g.modes[g.mode_count++];
        m->w = (int)sd->dwWidth;
        m->h = (int)sd->dwHeight;
        m->bpp = (int)sd->ddpfPixelFormat.dwRGBBitCount;
        m->hz = (int)sd->dwRefreshRate;
    }

    return DDENUMRET_OK;
}

static int compare_modes(const void* a, const void* b)
{
    const Mode* x = a;
    const Mode* y = b;
    if (x->w != y->w) return x->w - y->w;
    if (x->h != y->h) return x->h - y->h;
    if (x->bpp != y->bpp) return x->bpp - y->bpp;
    return x->hz - y->hz;
}

static void query_modes(void)
{
    g.error[0] = '\0';

    if (!load_ddraw())
    {
        update_status();
        return;
    }

    LPDIRECTDRAW7 dd = g.dd;
    BOOL temporary = FALSE;

    if (!dd)
    {
        HRESULT hr = g.DirectDrawCreateEx(NULL, (LPVOID*)&dd, &IID_IDirectDraw7, NULL);
        if (!dd_ok(hr, "DirectDrawCreateEx"))
        {
            update_status();
            return;
        }
        temporary = TRUE;
    }

    g.mode_count = 0;
    HRESULT hr = IDirectDraw7_EnumDisplayModes(dd, DDEDM_REFRESHRATES, NULL, NULL, enum_modes_cb);
    dd_ok(hr, "EnumDisplayModes");

    if (temporary)
        IDirectDraw7_Release(dd);

    qsort(g.modes, (size_t)g.mode_count, sizeof(Mode), compare_modes);

    ComboBox_ResetContent(g.cb_modes);
    for (int i = 0; i < g.mode_count; i++)
    {
        char buf[64];
        if (g.modes[i].hz)
            _snprintf(buf, sizeof(buf) - 1, "%d x %d, %d-bit, %d Hz", g.modes[i].w, g.modes[i].h, g.modes[i].bpp, g.modes[i].hz);
        else
            _snprintf(buf, sizeof(buf) - 1, "%d x %d, %d-bit", g.modes[i].w, g.modes[i].h, g.modes[i].bpp);
        buf[sizeof(buf) - 1] = '\0';
        ComboBox_AddString(g.cb_modes, buf);
    }

    if (g.mode_count > 0)
        ComboBox_ShowDropdown(g.cb_modes, TRUE);
    else if (!g.error[0])
        set_error("DirectDraw reported no display modes");

    update_status();
}

static void use_mode(int index)
{
    if (index < 0 || index >= g.mode_count)
        return;

    const Mode* m = &g.modes[index];
    char buf[16];

    _snprintf(buf, sizeof(buf) - 1, "%d", m->w); buf[sizeof(buf) - 1] = '\0';
    SetWindowTextA(g.ed_width, buf);
    _snprintf(buf, sizeof(buf) - 1, "%d", m->h); buf[sizeof(buf) - 1] = '\0';
    SetWindowTextA(g.ed_height, buf);
    _snprintf(buf, sizeof(buf) - 1, "%d", m->hz); buf[sizeof(buf) - 1] = '\0';
    SetWindowTextA(g.ed_refresh, buf);

    for (int i = 0; i < BPP_COUNT; i++)
        if (BPP_VALUES[i] == m->bpp)
            ComboBox_SetCurSel(g.cb_bpp, i);
}

static void change_pattern(int delta)
{
    g.cur.pattern = (g.cur.pattern + delta + PAT_COUNT) % PAT_COUNT;
    ComboBox_SetCurSel(g.cb_pattern, g.cur.pattern);
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        g.panel = hwnd;
        create_controls();
        SetTimer(hwnd, TIMER_STATUS, 500, NULL);
        return 0;

    case WM_TIMER:
        update_status();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_APPLY: apply(); return 0;
        case ID_STOP: g.error[0] = '\0'; stop(); return 0;
        case ID_BROWSE: browse_image(); return 0;
        case ID_IMGSIZE: use_image_size(); return 0;
        case ID_QUERY: query_modes(); return 0;

        case ID_MODES:
            if (HIWORD(wp) == CBN_SELCHANGE)
                use_mode(ComboBox_GetCurSel(g.cb_modes));
            return 0;

        /* These take effect immediately; everything else waits for Apply */
        case ID_PATTERN:
            if (HIWORD(wp) == CBN_SELCHANGE && g.running)
                g.cur.pattern = ComboBox_GetCurSel(g.cb_pattern);
            return 0;
        case ID_INFO:
            if (g.running) g.cur.info = Button_GetCheck(g.chk_info) == BST_CHECKED;
            return 0;
        case ID_MOUSE:
            if (g.running) g.cur.mouse = Button_GetCheck(g.chk_mouse) == BST_CHECKED;
            return 0;
        case ID_FIT:
            if (g.running) g.cur.image_fit = Button_GetCheck(g.chk_fit) == BST_CHECKED;
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_STATUS);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK display_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        switch (wp)
        {
        case VK_ESCAPE:
            stop();
            return 0;
        case VK_RIGHT:
            change_pattern(1);
            return 0;
        case VK_LEFT:
            change_pattern(-1);
            return 0;
        case 'I':
            g.cur.info = !g.cur.info;
            Button_SetCheck(g.chk_info, g.cur.info ? BST_CHECKED : BST_UNCHECKED);
            return 0;
        case 'M':
            g.cur.mouse = !g.cur.mouse;
            Button_SetCheck(g.chk_mouse, g.cur.mouse ? BST_CHECKED : BST_UNCHECKED);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        g.clicks[g.click_next].x = GET_X_LPARAM(lp);
        g.clicks[g.click_next].y = GET_Y_LPARAM(lp);
        g.click_next = (g.click_next + 1) % MAX_CLICKS;
        if (g.click_count < MAX_CLICKS)
            g.click_count++;
        return 0;

    case WM_RBUTTONDOWN:
        g.click_count = 0;
        g.click_next = 0;
        return 0;

    case WM_SETCURSOR:
        /* Fullscreen games usually hide the Windows cursor and draw their own */
        if (LOWORD(lp) == HTCLIENT && g.running && g.cur.present != PRESENT_WINDOWED)
        {
            SetCursor(NULL);
            return TRUE;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        stop();
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------- */

static void clamp_settings(Settings* s)
{
    if (s->width < 1) s->width = 1;
    if (s->width > MAX_DIMENSION) s->width = MAX_DIMENSION;
    if (s->height < 1) s->height = 1;
    if (s->height > MAX_DIMENSION) s->height = MAX_DIMENSION;
    if (s->bpp != 8 && s->bpp != 16 && s->bpp != 24 && s->bpp != 32) s->bpp = 8;
    if (s->refresh < 0) s->refresh = 0;
    if (s->fps < 0) s->fps = 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)prev;
    (void)cmdline;

    g.inst = inst;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    timeBeginPeriod(1);
    QueryPerformanceFrequency(&g.qpf);

    palette_build(g.base_pal);
    palette_build_lut(g.base_pal);

    GetModuleFileNameA(NULL, g.exe_dir, sizeof(g.exe_dir));
    char* slash = strrchr(g.exe_dir, '\\');
    if (slash)
        slash[1] = '\0';
    _snprintf(g.ini_path, sizeof(g.ini_path) - 1, "%sddtest.ini", g.exe_dir);

    Settings s;
    settings_default(&s);
    settings_load(&s);

    BOOL start_now = FALSE;
    char dump_dir[MAX_PATH] = "";
    uint32_t dump_frame = 0;
    int only_pattern = -1;

    if (!parse_args(&s, &start_now, dump_dir, &dump_frame, &only_pattern))
        return 2;

    clamp_settings(&s);

    if (dump_dir[0])
    {
        int rc = run_dump(&s, dump_dir, dump_frame, only_pattern);
        cv_shutdown();
        timeEndPeriod(1);
        CoUninitialize();
        return rc;
    }

    load_ddraw();

    NONCLIENTMETRICSA ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectA(&ncm.lfMessageFont);

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "DDTestPanel";
    RegisterClassA(&wc);

    wc.lpfnWndProc = display_proc;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "DDTestDisplay";
    RegisterClassA(&wc);

    g.display = CreateWindowExA(0, "DDTestDisplay", "ddtest", WS_POPUP, 0, 0, 640, 480,
                                NULL, NULL, inst, NULL);

    DWORD panel_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, PANEL_W, PANEL_H };
    AdjustWindowRectEx(&r, panel_style, FALSE, 0);

    CreateWindowExA(0, "DDTestPanel", "ddtest - DirectDraw test patterns", panel_style,
                    CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                    NULL, NULL, inst, NULL);

    if (!g.panel || !g.display)
        return 1;

    write_panel(&s);
    update_status();
    ShowWindow(g.panel, show);
    UpdateWindow(g.panel);

    if (start_now)
        apply();

    int exit_code = 0;
    MSG msg;
    msg.wParam = 0;

    for (;;)
    {
        if (g.quit_after_frames && (!g.running || g.frame >= g.quit_after_frames))
        {
            exit_code = g.running ? 0 : 3;

            if (g.log_path[0])
            {
                FILE* f = fopen(g.log_path, "w");
                if (f)
                {
                    if (g.running)
                        fprintf(f, "ok: %u frames, %d x %d, %d-bit, %s, %.1f fps\nddraw.dll: %s\n", g.frame,
                                g.cur.width, g.cur.height, g.cur.bpp, PRESENT_SHORT[g.cur.present], g.fps, g.ddraw_path);
                    else
                        fprintf(f, "error: %s\nddraw.dll: %s\n", g.error[0] ? g.error : "stopped", g.ddraw_path);
                    fclose(f);
                }
            }
            break;
        }

        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto done;

            if (!IsDialogMessageA(g.panel, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }

        if (g.running)
        {
            render_frame();
            wait_for_next_frame();
        }
        else
        {
            WaitMessage();
        }
    }

done:
    stop();
    if (g.display)
        DestroyWindow(g.display);
    free(g.image);
    free(g.canvas.px);
    free(g.canvas.idx);
    cv_shutdown();
    if (g.font)
        DeleteObject(g.font);
    timeEndPeriod(1);
    CoUninitialize();
    return g.quit_after_frames ? exit_code : (int)msg.wParam;
}
