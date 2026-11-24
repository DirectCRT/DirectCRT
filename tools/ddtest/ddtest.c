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
 *   --ddraw 1|2|4|7  --present flip|blt|vblank|lock|windowed
 *   --pattern NAME|NUMBER  --image FILE
 *   --start          run straight away
 *   --dump DIR       save every pattern (or just --pattern) as PNG files and exit,
 *                    without using DirectDraw; --frame N picks the animation frame
 *   --frames N       with --start: exit after N frames (exit code 0), or with
 *                    exit code 3 if DirectDraw failed, 4 if the blits
 *                    pattern's self-check failed
 *   --log FILE       with --frames: write the result to FILE
 */

#include "ddtest.h"
#include "resource.h"

#include <commctrl.h>
#include <commdlg.h>
#include <math.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Current-looking controls (and drop-down hint text) from Common Controls 6 */
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

#ifndef CB_SETCUEBANNER
#define CB_SETCUEBANNER 0x1703
#endif

#include <initguid.h>
#include <ddraw.h>

/* ---------------------------------------------------------------------- */
/* Settings                                                                */

enum { PRESENT_FLIP, PRESENT_BLT, PRESENT_VBLANK, PRESENT_LOCK, PRESENT_WINDOWED, PRESENT_COUNT };

/* What the program asks DirectDraw for. DirectCRT follows it with
   display=auto; display=window/fullscreen override it. */
static const char* PRESENT_NAMES[PRESENT_COUNT] = {
    "Fullscreen, page flipping",
    "Fullscreen, Blt to the primary surface",
    "Fullscreen, WaitForVerticalBlank, then Blt to the primary",
    "Fullscreen, write the primary surface",
    "Windowed, Blt to the window",
};

static const char* PRESENT_SHORT[PRESENT_COUNT] = { "flip", "blt", "vblank", "lock", "windowed" };

static const char* PRESENT_DESC[PRESENT_COUNT] = {
    "page flipping", "Blt to primary", "vblank, then Blt", "writing the primary", "windowed",
};

/* The DirectDraw interface a game asks for */
static const int DDRAW_VERSIONS[] = { 1, 2, 4, 7 };
static const char* DDRAW_NAMES[] = { "IDirectDraw", "IDirectDraw2", "IDirectDraw4", "IDirectDraw7" };
#define DDRAW_COUNT ((int)(sizeof(DDRAW_VERSIONS) / sizeof(DDRAW_VERSIONS[0])))

static const char* ddraw_name(int version)
{
    for (int i = 0; i < DDRAW_COUNT; i++)
        if (DDRAW_VERSIONS[i] == version)
            return DDRAW_NAMES[i];
    return "?";
}

static const int BPP_VALUES[] = { 8, 16, 24, 32 };
#define BPP_COUNT ((int)(sizeof(BPP_VALUES) / sizeof(BPP_VALUES[0])))

#define MAX_DIMENSION 8192

typedef struct Settings
{
    int width, height, bpp, refresh, fps;
    int ddraw; /* 1, 2, 4 or 7 */
    int present, pattern;
    BOOL info, mouse;
    char image_path[MAX_PATH];
} Settings;

static void settings_default(Settings* s)
{
    ZeroMemory(s, sizeof(*s));
    s->width = 640;
    s->height = 480;
    s->bpp = 8;
    s->fps = 60;
    s->ddraw = 7;
    s->present = PRESENT_FLIP;
    s->pattern = PAT_COLOR_BARS;
    s->info = TRUE;
    s->mouse = TRUE;
}

/* ---------------------------------------------------------------------- */
/* App state                                                               */

enum
{
    ID_RUN = IDOK,
    ID_STOP = 1001,
    ID_RES, ID_WIDTH, ID_HEIGHT, ID_REFRESH, ID_FPS,
    ID_PATTERN, ID_INFO, ID_MOUSE,
    ID_IMAGE, ID_BROWSE, ID_IMGSIZE,
    ID_MODES, ID_QUERY, ID_STATUS,
    ID_BPP0 = 1100,     /* radio buttons, one per BPP_VALUES entry */
    ID_PRESENT0 = 1110, /* radio buttons, one per presentation method */
    ID_DDRAW0 = 1120,   /* radio buttons, one per DDRAW_VERSIONS entry */
};

/* Posted to the panel when a text box loses the focus, so its value is
   applied once the focus has settled */
#define WM_APP_COMMIT (WM_APP + 1)

/* Posted to the panel when the test window has been resized to (wParam, lParam) */
#define WM_APP_RESIZED (WM_APP + 2)

#define TIMER_STATUS 1
#define MAX_CLICKS 16
#define MAX_MODES 512

typedef struct Mode { int w, h, bpp, hz; } Mode;

/* The resolution list; anything else is "Custom" */
static const SIZE RESOLUTIONS[] = {
    { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 400 }, { 640, 480 },
    { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 }, { 1280, 960 }, { 1280, 1024 },
    { 1366, 768 }, { 1600, 1200 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 }, { 3840, 2160 },
};
#define RES_COUNT ((int)(sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0])))
#define RES_CUSTOM RES_COUNT /* the last entry in the list */

static struct
{
    HINSTANCE inst;
    HFONT font;
    HWND panel, display;
    HWND cb_res, ed_width, ed_height, rb_bpp[BPP_COUNT], ed_refresh, ed_fps;
    HWND rb_ddraw[DDRAW_COUNT], rb_present[PRESENT_COUNT], cb_pattern, chk_info, chk_mouse;
    HWND ed_image, cb_modes, sb_status, lb_dll, st_dll;
    HWND bt_browse, bt_imgsize, bt_query, bt_run, bt_stop;
    HWND lb_size, lb_x, lb_bpp, lb_refresh, lb_hz, lb_fps, lb_fps_unit;
    HWND lb_ddraw, lb_present, lb_pattern, lb_image, st_image_note, lb_modes;
    int dpi;

    char exe_dir[MAX_PATH];
    char ini_path[MAX_PATH];

    HMODULE ddraw_mod;
    char ddraw_path[MAX_PATH + 32];
    HRESULT (WINAPI* DirectDrawCreate)(GUID*, LPDIRECTDRAW*, IUnknown*);
    HRESULT (WINAPI* DirectDrawCreateEx)(GUID*, LPVOID*, REFIID, IUnknown*);

    /* Any interface version, held as version 7 (see dd_create) */
    LPDIRECTDRAW7 dd;
    int dd_version;
    BOOL dd_fullscreen;
    LPDIRECTDRAWSURFACE7 primary, back, offscreen;
    LPDIRECTDRAWSURFACE7 sprite; /* the blits pattern's sprite */
    int sprite_size;
    LPDIRECTDRAWPALETTE pal;
    BOOL pal_dirty;
    LPDIRECTDRAWCLIPPER clipper;
    DDPIXELFORMAT pf;
    int primary_bpp;

    BOOL running;
    BOOL unload_pending;
    BOOL restart_pending; /* run again once ddraw.dll is unloaded */
    int pan_x, pan_y;     /* image patterns: Ctrl+arrows move the picture */
    POINT window_pos;     /* where the next windowed test window goes, if have_window_pos */
    BOOL have_window_pos;
    HWND restart_focus;   /* the panel control to give the focus back to after it */
    Settings cur;
    Canvas canvas;
    uint32_t base_pal[256];

    uint32_t* image;
    int image_w, image_h;
    char image_loaded_path[MAX_PATH];

    uint32_t frame;
    LARGE_INTEGER qpf, fps_t0, next_frame, run_t0;
    int fps_frames;
    double fps;

    POINT clicks[MAX_CLICKS];
    int click_count, click_next;

    Mode modes[MAX_MODES];
    int mode_count;

    uint32_t quit_after_frames;
    char log_path[MAX_PATH];

    /* The blits pattern's self-check since the run or mode change */
    uint32_t check_frames, check_failed;
    BOOL check_frame_failed;
    char check_msg[320]; /* the first failure */

    char error[512];
} g;

static void update_status(void);
static void pan_image(int dx, int dy);

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
    case DDERR_EXCEPTION: return "DDERR_EXCEPTION";
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

static void append_error(const char* fmt, ...)
{
    size_t len = strlen(g.error);
    if (len >= sizeof(g.error) - 1)
        return;

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(g.error + len, sizeof(g.error) - 1 - len, fmt, ap);
    g.error[sizeof(g.error) - 1] = '\0';
    va_end(ap);
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
        return g.DirectDrawCreate != NULL;

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

    g.DirectDrawCreate = (HRESULT (WINAPI*)(GUID*, LPDIRECTDRAW*, IUnknown*))
        GetProcAddress(g.ddraw_mod, "DirectDrawCreate");
    g.DirectDrawCreateEx = (HRESULT (WINAPI*)(GUID*, LPVOID*, REFIID, IUnknown*))
        GetProcAddress(g.ddraw_mod, "DirectDrawCreateEx");

    if (!g.DirectDrawCreate)
    {
        set_error("This ddraw.dll has no DirectDrawCreate");
        return FALSE;
    }

    return TRUE;
}

/* Unloading after every run makes the next Run start like a new game: the
   DLL reads its settings again and forgets runtime changes such as Alt+Enter.
   It's done from the main loop, never inside a window message, because a
   wrapper's own window procedure may still be on the call stack. */
static void unload_ddraw(void)
{
    if (!g.ddraw_mod)
        return;

    FreeLibrary(g.ddraw_mod);
    g.ddraw_mod = NULL;
    g.DirectDrawCreate = NULL;
    g.DirectDrawCreateEx = NULL;
}

/* ---------------------------------------------------------------------- */
/* DirectDraw interface versions                                           */

/* --ddraw picks the interface a game would use: IDirectDraw (DirectX 1 to
   3), created with DirectDrawCreate; IDirectDraw2 (DirectX 5) or
   IDirectDraw4 (DirectX 6), asked for with QueryInterface on that; or
   IDirectDraw7, created with DirectDrawCreateEx. Each version adds methods
   at the end of the one before, and the methods ddtest calls take the same
   arguments in all of them, except SetDisplayMode and RestoreAllSurfaces
   (below). The surface descriptions differ in size, and dwSize tells
   DirectDraw which one it has (dd_desc_size). So objects of every version
   are kept, and called, as version 7 ones. The surfaces are those of the
   version that created them: IDirectDrawSurface for 1 and 2,
   IDirectDrawSurface4 for 4. */
static BOOL dd_create(int version, LPDIRECTDRAW7* out)
{
    *out = NULL;

    if (version == 7)
    {
        if (!g.DirectDrawCreateEx)
        {
            set_error("This ddraw.dll has no DirectDrawCreateEx");
            return FALSE;
        }

        return dd_ok(g.DirectDrawCreateEx(NULL, (LPVOID*)out, &IID_IDirectDraw7, NULL), "DirectDrawCreateEx");
    }

    LPDIRECTDRAW dd1 = NULL;
    if (!dd_ok(g.DirectDrawCreate(NULL, &dd1, NULL), "DirectDrawCreate"))
        return FALSE;

    if (version == 1)
    {
        *out = (LPDIRECTDRAW7)dd1;
        return TRUE;
    }

    HRESULT hr = IDirectDraw_QueryInterface(dd1, version == 2 ? &IID_IDirectDraw2 : &IID_IDirectDraw4, (LPVOID*)out);
    IDirectDraw_Release(dd1);

    if (FAILED(hr))
        *out = NULL;

    return dd_ok(hr, version == 2 ? "QueryInterface (IDirectDraw2)" : "QueryInterface (IDirectDraw4)");
}

/* DDSURFACEDESC for the first three versions, DDSURFACEDESC2 after. The
   first is the start of the second, so a DDSURFACEDESC2 holds either. */
static DWORD dd_desc_size(void)
{
    return g.dd_version >= 4 ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC);
}

/* IDirectDraw's has no refresh rate or flags */
static HRESULT dd_set_display_mode(const Settings* s)
{
    if (g.dd_version == 1)
        return IDirectDraw_SetDisplayMode((LPDIRECTDRAW)g.dd, (DWORD)s->width, (DWORD)s->height, (DWORD)s->bpp);

    return IDirectDraw7_SetDisplayMode(g.dd, (DWORD)s->width, (DWORD)s->height, (DWORD)s->bpp, (DWORD)s->refresh, 0);
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

/* One colour in the surface's pixel format, as convert_to_surface writes it */
static uint32_t surface_value(uint32_t p)
{
    const DDPIXELFORMAT* pf = &g.pf;

    if (pf->dwFlags & DDPF_PALETTEINDEXED8)
        return palette_lookup(p);

    return ((COL_R(p) >> (8 - mask_bits(pf->dwRBitMask))) << mask_shift(pf->dwRBitMask)) |
           ((COL_G(p) >> (8 - mask_bits(pf->dwGBitMask))) << mask_shift(pf->dwGBitMask)) |
           ((COL_B(p) >> (8 - mask_bits(pf->dwBBitMask))) << mask_shift(pf->dwBBitMask));
}

/* What convert_to_surface wrote for a canvas pixel */
static uint32_t canvas_value(int x, int y)
{
    const Canvas* c = &g.canvas;
    int i = y * c->w + x;

    if (g.pf.dwFlags & DDPF_PALETTEINDEXED8)
        return c->use_idx ? c->idx[i] : palette_lookup(c->px[i]);

    return surface_value(c->px[i]);
}

static uint32_t read_value(const BYTE* base, LONG pitch, int x, int y)
{
    const BYTE* row = base + (size_t)y * pitch;

    switch (g.pf.dwRGBBitCount)
    {
    case 8: return row[x];
    case 16: return ((const uint16_t*)row)[x];
    case 24: return row[x * 3] | ((uint32_t)row[x * 3 + 1] << 8) | ((uint32_t)row[x * 3 + 2] << 16);
    default: return ((const uint32_t*)row)[x];
    }
}

static void write_value(BYTE* base, LONG pitch, int x, int y, uint32_t v)
{
    BYTE* row = base + (size_t)y * pitch;

    switch (g.pf.dwRGBBitCount)
    {
    case 8: row[x] = (BYTE)v; break;
    case 16: ((uint16_t*)row)[x] = (uint16_t)v; break;
    case 24: row[x * 3] = (BYTE)v; row[x * 3 + 1] = (BYTE)(v >> 8); row[x * 3 + 2] = (BYTE)(v >> 16); break;
    default: ((uint32_t*)row)[x] = v; break;
    }
}

/* The same colour on the screen. In 8-bit modes that's the palette entries'
   colours: GDI may pick another entry of the same colour. */
static BOOL same_colour(uint32_t a, uint32_t b)
{
    if (g.pf.dwFlags & DDPF_PALETTEINDEXED8)
        return g.base_pal[a & 255] == g.base_pal[b & 255];

    return ((a ^ b) & (g.pf.dwRBitMask | g.pf.dwGBitMask | g.pf.dwBBitMask)) == 0;
}

static const char* value_text(uint32_t v, char* buf, int len)
{
    if (g.pf.dwFlags & DDPF_PALETTEINDEXED8)
        _snprintf(buf, len - 1, "index %u (#%06X)", v & 255, g.base_pal[v & 255]);
    else
        _snprintf(buf, len - 1, "0x%0*X", (int)(g.pf.dwRGBBitCount + 3) / 4, v);
    buf[len - 1] = '\0';
    return buf;
}

/* ---------------------------------------------------------------------- */
/* DirectDraw setup and teardown                                           */

static void release_surfaces(void)
{
    if (g.sprite) { IDirectDrawSurface7_Release(g.sprite); g.sprite = NULL; }
    g.sprite_size = 0;
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

/* Every DirectDraw session gets a new test window, the way every game start
   does, so nothing a wrapper did to the last one carries over */
static BOOL create_display(void)
{
    if (!g.display)
        g.display = CreateWindowExA(0, "DDTestDisplay", "ddtest", WS_POPUP, 0, 0, 640, 480, NULL, NULL, g.inst, NULL);

    if (!g.display)
        set_error("Couldn't create the test window (error %lu)", GetLastError());

    return g.display != NULL;
}

static void destroy_display(void)
{
    HWND h = g.display;
    g.display = NULL;

    if (h)
        DestroyWindow(h);
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
        /* Resizable: a new size is a new resolution (see WM_EXITSIZEMOVE) */
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
        RECT r = { 0, 0, s->width, s->height };
        AdjustWindowRectEx(&r, style, FALSE, 0);

        RECT work;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);

        int ww = r.right - r.left, wh = r.bottom - r.top;
        int x = work.left + ((work.right - work.left) - ww) / 2;
        int y = work.top + ((work.bottom - work.top) - wh) / 2;

        /* After the user resized the window, the new one goes where that was */
        if (g.have_window_pos)
        {
            x = g.window_pos.x;
            y = g.window_pos.y;
            g.have_window_pos = FALSE;
        }
        else
        {
            if (x < work.left) x = work.left;
            if (y < work.top) y = work.top;
        }

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
    sd.dwSize = dd_desc_size();

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

    if (s->present == PRESENT_BLT || s->present == PRESENT_VBLANK || s->present == PRESENT_WINDOWED)
    {
        DDPIXELFORMAT primary_pf;
        ZeroMemory(&primary_pf, sizeof(primary_pf));
        primary_pf.dwSize = sizeof(primary_pf);
        IDirectDrawSurface7_GetPixelFormat(g.primary, &primary_pf);
        g.primary_bpp = (int)primary_pf.dwRGBBitCount;

        ZeroMemory(&sd, sizeof(sd));
        sd.dwSize = dd_desc_size();
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
    {
        release_dd();
        destroy_display();
    }

    if (!create_display())
        return FALSE;

    setup_window(s, fullscreen);

    if (!g.dd)
    {
        if (!dd_create(s->ddraw, &g.dd))
            return FALSE;
        g.dd_version = s->ddraw;

        hr = IDirectDraw7_SetCooperativeLevel(g.dd, g.display,
            fullscreen ? (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT) : DDSCL_NORMAL);
        if (!dd_ok(hr, "SetCooperativeLevel"))
            return FALSE;

        g.dd_fullscreen = fullscreen;
    }

    if (fullscreen)
    {
        hr = dd_set_display_mode(s);
        if (!dd_ok(hr, "SetDisplayMode"))
        {
            if (s->bpp == 24)
                append_error(". This ddraw.dll has no 24-bit display modes: Windows' own DirectDraw "
                             "doesn't offer them on current graphics cards (DirectCRT does).");
            else if (hr == DDERR_INVALIDMODE || hr == DDERR_UNSUPPORTEDMODE)
                append_error(". This ddraw.dll doesn't offer that mode; List shows the ones it does.");
            return FALSE;
        }

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
    g.check_frames = g.check_failed = 0;
    g.check_msg[0] = '\0';
    QueryPerformanceCounter(&g.fps_t0);
    g.run_t0 = g.fps_t0;
    return TRUE;
}

static void stop(void)
{
    release_dd();
    g.running = FALSE;
    destroy_display();
    g.unload_pending = TRUE;

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

    _snprintf(line, sizeof(line) - 1, "%d x %d, %d-bit, %s, %s", g.cur.width, g.cur.height, g.cur.bpp,
              PRESENT_DESC[g.cur.present], ddraw_name(g.cur.ddraw));
    line[sizeof(line) - 1] = '\0';
    y += cv_label(c, 3, y, ts, WHITE, BLACK, line);

    _snprintf(line, sizeof(line) - 1, "%s - frame %u - %.1f fps", pattern_name(g.cur.pattern), g.frame, g.fps);
    line[sizeof(line) - 1] = '\0';
    y += cv_label(c, 3, y, ts, WHITE, BLACK, line);

    _snprintf(line, sizeof(line) - 1, "ddraw.dll: %s", g.ddraw_path);
    line[sizeof(line) - 1] = '\0';
    y += cv_label(c, 3, y, ts, WHITE, BLACK, line);

    if (g.cur.pattern == PAT_TEST_IMAGE || g.cur.pattern == PAT_IMAGE)
    {
        _snprintf(line, sizeof(line) - 1, "Picture moved %d, %d from centred", g.pan_x, g.pan_y);
        line[sizeof(line) - 1] = '\0';
        y += cv_label(c, 3, y, ts, WHITE, BLACK, line);
    }

    if (g.cur.pattern == PAT_BLITS && g.check_frames)
    {
        if (g.check_failed)
            _snprintf(line, sizeof(line) - 1, "Self-check FAILED in %u of %u frames: %s", g.check_failed,
                      g.check_frames, g.check_msg);
        else
            _snprintf(line, sizeof(line) - 1, "Self-check passed, %u frames", g.check_frames);
        line[sizeof(line) - 1] = '\0';
        y += cv_label(c, 3, y, ts, g.check_failed ? COL(255, 102, 102) : COL(102, 255, 102), BLACK, line);
    }

    cv_label(c, 3, y, ts, COL(160, 160, 160), BLACK,
             g.cur.pattern == PAT_TEST_IMAGE || g.cur.pattern == PAT_IMAGE
                 ? "Esc: stop  Left/Right: pattern  Ctrl+arrows: move (+Shift: 1 pixel)  I: info  M: mouse"
                 : "Esc: stop  Left/Right: pattern  I: info  M: mouse");
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
    HRESULT hr;

    if (g.dd_version >= 4)
    {
        hr = IDirectDraw7_RestoreAllSurfaces(g.dd);
    }
    else
    {
        /* IDirectDraw and IDirectDraw2 have no RestoreAllSurfaces; the back
           buffer comes back with the primary */
        hr = IDirectDrawSurface7_Restore(g.primary);
        if (SUCCEEDED(hr) && g.offscreen)
            hr = IDirectDrawSurface7_Restore(g.offscreen);
        if (SUCCEEDED(hr) && g.sprite)
            hr = IDirectDrawSurface7_Restore(g.sprite);
    }

    if (FAILED(hr))
        Sleep(50); /* e.g. while alt-tabbed out of exclusive fullscreen */
}

/* 8-bit modes: the palette pattern cycles the colour cube's entries, the
   fade pattern scales all 256; the others get the base palette back */
static void update_palette(void)
{
    BOOL cycle = g.cur.pattern == PAT_PALETTE, fade = g.cur.pattern == PAT_FADE;

    if (!g.pal || (!cycle && !fade && !g.pal_dirty))
        return;

    int level = fade ? fade_level(g.frame) : 256;
    PALETTEENTRY pe[256];

    for (int i = 0; i < 256; i++)
    {
        uint32_t col = cycle && i < PAL_CYCLE_COUNT ? g.base_pal[(i + g.frame) % PAL_CYCLE_COUNT] : g.base_pal[i];
        pe[i].peRed = (BYTE)(COL_R(col) * level / 256);
        pe[i].peGreen = (BYTE)(COL_G(col) * level / 256);
        pe[i].peBlue = (BYTE)(COL_B(col) * level / 256);
        pe[i].peFlags = 0;
    }

    IDirectDrawPalette_SetEntries(g.pal, 0, 0, cycle ? PAL_CYCLE_COUNT : 256, pe);
    g.pal_dirty = cycle || fade;
}

/* ---------------------------------------------------------------------- */
/* The blits pattern: blits and GDI text on the surface, read back and     */
/* checked                                                                 */

/* The first failure in a frame counts; the first of the run is kept */
static void check_fail(const char* fmt, ...)
{
    if (g.check_frame_failed)
        return;

    g.check_frame_failed = TRUE;

    if (g.check_msg[0])
        return;

    int n = _snprintf(g.check_msg, sizeof(g.check_msg) - 1, "frame %u: ", g.frame);
    if (n < 0)
        n = 0;

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(g.check_msg + n, sizeof(g.check_msg) - 1 - n, fmt, ap);
    g.check_msg[sizeof(g.check_msg) - 1] = '\0';
    va_end(ap);
}

/* An off-screen surface in the target's format with the source colour key
   set. It's drawn every frame, which also tests locking a second surface. */
static BOOL prepare_sprite(int size)
{
    HRESULT hr;

    if (g.sprite && g.sprite_size != size)
    {
        IDirectDrawSurface7_Release(g.sprite);
        g.sprite = NULL;
    }

    if (!g.sprite)
    {
        DDSURFACEDESC2 sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.dwSize = dd_desc_size();
        sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
        sd.dwWidth = sd.dwHeight = (DWORD)size;
        sd.ddpfPixelFormat = g.pf;

        hr = IDirectDraw7_CreateSurface(g.dd, &sd, &g.sprite, NULL);
        if (FAILED(hr))
        {
            g.sprite = NULL;
            check_fail("CreateSurface (sprite) failed: %s", dderr_name(hr));
            return FALSE;
        }
        g.sprite_size = size;

        DDCOLORKEY key;
        key.dwColorSpaceLowValue = key.dwColorSpaceHighValue = surface_value(BLITS_KEY);
        hr = IDirectDrawSurface7_SetColorKey(g.sprite, DDCKEY_SRCBLT, &key);
        if (FAILED(hr))
        {
            check_fail("SetColorKey failed: %s", dderr_name(hr));
            return FALSE;
        }
    }

    DDSURFACEDESC2 sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.dwSize = dd_desc_size();

    hr = IDirectDrawSurface7_Lock(g.sprite, NULL, (LPDDSURFACEDESC2)&sd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    if (FAILED(hr))
    {
        check_fail("Lock (sprite) failed: %s", dderr_name(hr));
        return FALSE;
    }

    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            write_value((BYTE*)sd.lpSurface, sd.lPitch, x, y, surface_value(blits_sprite_pixel(x, y, size)));

    IDirectDrawSurface7_Unlock(g.sprite, NULL);
    return TRUE;
}

static BOOL rect_inside(const RECT* r, int w, int h)
{
    return r->left >= 0 && r->top >= 0 && r->right <= w && r->bottom <= h && r->left < r->right && r->top < r->bottom;
}

/* Colour fills, the sprite with Blt and BltFast and a source colour key,
   then GDI text, all on the frame the canvas was written to. Then the
   surface is read back: the fills and the sprites must be exact (the key
   letting the canvas through), the text only white on black. */
static void blits_draw_and_check(LPDIRECTDRAWSURFACE7 target)
{
    BlitsLayout l;
    blits_layout(g.cur.width, g.cur.height, g.frame, &l);

    int w = g.cur.width, h = g.cur.height;
    HRESULT hr;
    BOOL lost = FALSE;
    g.check_frame_failed = FALSE;

    for (int i = 0; i < 4; i++)
    {
        if (!rect_inside(&l.fill[i], w, h))
            continue;

        DDBLTFX fx;
        ZeroMemory(&fx, sizeof(fx));
        fx.dwSize = sizeof(fx);
        fx.dwFillColor = surface_value(l.fill_color[i]);

        hr = IDirectDrawSurface7_Blt(target, &l.fill[i], NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
        lost |= hr == DDERR_SURFACELOST;
        if (FAILED(hr))
            check_fail("Blt (colour fill) failed: %s", dderr_name(hr));
    }

    RECT sprite_rect[2];
    BOOL sprite_drawn[2] = { FALSE, FALSE };
    for (int i = 0; i < 2; i++)
    {
        sprite_rect[i].left = l.sprite_pos[i].x;
        sprite_rect[i].top = l.sprite_pos[i].y;
        sprite_rect[i].right = l.sprite_pos[i].x + l.sprite_size;
        sprite_rect[i].bottom = l.sprite_pos[i].y + l.sprite_size;
    }

    if (prepare_sprite(l.sprite_size))
    {
        if (rect_inside(&sprite_rect[0], w, h))
        {
            hr = IDirectDrawSurface7_Blt(target, &sprite_rect[0], g.sprite, NULL, DDBLT_KEYSRC | DDBLT_WAIT, NULL);
            lost |= hr == DDERR_SURFACELOST;
            if (FAILED(hr))
                check_fail("Blt (source colour key) failed: %s", dderr_name(hr));
            sprite_drawn[0] = SUCCEEDED(hr);
        }

        if (rect_inside(&sprite_rect[1], w, h))
        {
            hr = IDirectDrawSurface7_BltFast(target, (DWORD)sprite_rect[1].left, (DWORD)sprite_rect[1].top, g.sprite,
                                             NULL, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
            lost |= hr == DDERR_SURFACELOST;
            if (FAILED(hr))
                check_fail("BltFast (source colour key) failed: %s", dderr_name(hr));
            sprite_drawn[1] = SUCCEEDED(hr);
        }
    }

    RECT text_rect[BLITS_LINES];
    int text_count = 0;
    HDC dc;

    hr = IDirectDrawSurface7_GetDC(target, &dc);
    lost |= hr == DDERR_SURFACELOST;
    if (FAILED(hr))
    {
        check_fail("GetDC failed: %s", dderr_name(hr));
    }
    else
    {
        HFONT font = CreateFontA(-l.text_height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                                 DEFAULT_PITCH | FF_SWISS, "Arial");
        HGDIOBJ old_font = SelectObject(dc, font);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkColor(dc, RGB(0, 0, 0));
        SetBkMode(dc, OPAQUE);

        for (int i = 0; i < BLITS_LINES; i++)
        {
            char text[64];
            blits_text(i, g.frame, text, sizeof(text));
            int len = (int)strlen(text);
            int x = l.gdi_area.left + 4, y = l.gdi_area.top + 4 + i * (l.text_height + 4);

            TextOutA(dc, x, y, text, len);

            /* The opaque box TextOut fills: the text's extent */
            SIZE sz;
            GetTextExtentPoint32A(dc, text, len, &sz);
            RECT r = { x, y, x + sz.cx, y + sz.cy };
            if (r.right > w) r.right = w;
            if (r.bottom > h) r.bottom = h;
            if (r.left < r.right && r.top < r.bottom)
                text_rect[text_count++] = r;
        }

        SelectObject(dc, old_font);
        DeleteObject(font);
        IDirectDrawSurface7_ReleaseDC(target, dc);
    }

    DDSURFACEDESC2 sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.dwSize = dd_desc_size();

    hr = IDirectDrawSurface7_Lock(target, NULL, (LPDDSURFACEDESC2)&sd, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    lost |= hr == DDERR_SURFACELOST;

    /* Lost surfaces (alt-tabbed out) aren't a failure; the frame is skipped */
    if (lost)
    {
        if (SUCCEEDED(hr))
            IDirectDrawSurface7_Unlock(target, NULL);
        return;
    }

    if (FAILED(hr))
    {
        check_fail("Lock (to read back) failed: %s", dderr_name(hr));
    }
    else
    {
        const BYTE* base = (const BYTE*)sd.lpSurface;
        LONG pitch = sd.lPitch;
        char a[48], b[48];

        for (int i = 0; i < 4 && !g.check_frame_failed; i++)
        {
            if (!rect_inside(&l.fill[i], w, h))
                continue;

            uint32_t want = surface_value(l.fill_color[i]);
            for (int y = l.fill[i].top; y < l.fill[i].bottom && !g.check_frame_failed; y++)
                for (int x = l.fill[i].left; x < l.fill[i].right; x++)
                {
                    uint32_t v = read_value(base, pitch, x, y);
                    if (!same_colour(v, want))
                    {
                        check_fail("colour fill %d: pixel %d, %d is %s, should be %s", i + 1, x, y,
                                   value_text(v, a, sizeof(a)), value_text(want, b, sizeof(b)));
                        break;
                    }
                }
        }

        for (int i = 0; i < 2 && !g.check_frame_failed; i++)
        {
            if (!sprite_drawn[i])
                continue;

            for (int sy = 0; sy < l.sprite_size && !g.check_frame_failed; sy++)
                for (int sx = 0; sx < l.sprite_size; sx++)
                {
                    int x = sprite_rect[i].left + sx, y = sprite_rect[i].top + sy;
                    uint32_t sp = blits_sprite_pixel(sx, sy, l.sprite_size);
                    uint32_t want = sp == BLITS_KEY ? canvas_value(x, y) : surface_value(sp);
                    uint32_t v = read_value(base, pitch, x, y);

                    if (!same_colour(v, want))
                    {
                        check_fail("%s: pixel %d, %d is %s, should be %s (%s)", i ? "BltFast" : "Blt", x, y,
                                   value_text(v, a, sizeof(a)), value_text(want, b, sizeof(b)),
                                   sp == BLITS_KEY ? "the colour key lets the background through" : "the sprite");
                        break;
                    }
                }
        }

        uint32_t white = surface_value(WHITE), black = surface_value(BLACK);
        for (int i = 0; i < text_count && !g.check_frame_failed; i++)
        {
            int text_pixels = 0;

            for (int y = text_rect[i].top; y < text_rect[i].bottom && !g.check_frame_failed; y++)
                for (int x = text_rect[i].left; x < text_rect[i].right; x++)
                {
                    uint32_t v = read_value(base, pitch, x, y);
                    if (same_colour(v, white))
                    {
                        text_pixels++;
                    }
                    else if (!same_colour(v, black))
                    {
                        check_fail("GDI text: pixel %d, %d is %s, neither the white text nor its black background",
                                   x, y, value_text(v, a, sizeof(a)));
                        break;
                    }
                }

            if (!g.check_frame_failed && text_pixels == 0)
                check_fail("GDI text: line %d drew nothing", i + 1);
        }

        IDirectDrawSurface7_Unlock(target, NULL);
    }

    g.check_frames++;
    if (g.check_frame_failed)
        g.check_failed++;
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
    pan_image(0, 0); /* keep it within the picture after a resolution change */
    ctx.pan_x = g.pan_x;
    ctx.pan_y = g.pan_y;
    ctx.palette = g.base_pal;
    ctx.hw_palette = (g.pf.dwFlags & DDPF_PALETTEINDEXED8) != 0;
    ctx.image = g.image;
    ctx.image_w = g.image_w;
    ctx.image_h = g.image_h;
    ctx.app_blits = TRUE;

    /* The shape of the screen the picture fills, for the patterns that should
       look round or square. Windowed: the window, so square pixels.
       Fullscreen: square pixels too (1:1, 1280 x 1024 included), except for
       the low resolution VGA modes (up to 720 wide, below 480 lines, no
       wider than 16:10: 320 x 200, 640 x 400), which were shown on a 4:3
       screen. That's crt-auto's 'aspect=auto' rule. */
    double mode_aspect = (double)g.cur.width / g.cur.height;
    if (g.cur.present != PRESENT_WINDOWED &&
        g.cur.width <= 720 && g.cur.height < 480 && g.cur.width * 10 <= g.cur.height * 16)
        g.canvas.screen_aspect = 0; /* 4:3 */
    else
        g.canvas.screen_aspect = mode_aspect;

    pattern_draw(g.cur.pattern, &g.canvas, &ctx);

    if (g.cur.mouse)
        draw_mouse(&g.canvas);

    if (g.cur.info)
        draw_info(&g.canvas);

    update_palette();

    LPDIRECTDRAWSURFACE7 target = draw_target();
    DDSURFACEDESC2 sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.dwSize = dd_desc_size();

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

    if (g.cur.pattern == PAT_BLITS)
        blits_draw_and_check(target);

    const char* what = "Blt";

    switch (g.cur.present)
    {
    case PRESENT_FLIP:
        what = "Flip";
        hr = IDirectDrawSurface7_Flip(g.primary, NULL, DDFLIP_WAIT);
        break;

    case PRESENT_BLT:
        hr = IDirectDrawSurface7_Blt(g.primary, NULL, g.offscreen, NULL, DDBLT_WAIT, NULL);
        break;

    case PRESENT_VBLANK:
        what = "WaitForVerticalBlank";
        hr = IDirectDraw7_WaitForVerticalBlank(g.dd, DDWAITVB_BLOCKBEGIN, NULL);
        if (SUCCEEDED(hr))
        {
            what = "Blt";
            hr = IDirectDrawSurface7_Blt(g.primary, NULL, g.offscreen, NULL, DDBLT_WAIT, NULL);
        }
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
    else if (hr != DDERR_WASSTILLDRAWING && !dd_ok(hr, what))
    {
        if (g.cur.present == PRESENT_WINDOWED && g.primary_bpp != (int)g.pf.dwRGBBitCount)
            append_error(". This DirectDraw can't Blt from a %d-bit surface to the %d-bit desktop; "
                         "Windows' own DirectDraw doesn't convert between bit depths.",
                         (int)g.pf.dwRGBBitCount, g.primary_bpp);
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
    s->ddraw = GetPrivateProfileIntA(sec, "ddraw", s->ddraw, g.ini_path);

    /* The presentation and the pattern are saved by name. Older files have
       their numbers from before vblank, fade and blits were added. */
    static const int OLD_PRESENT[] = { PRESENT_FLIP, PRESENT_BLT, PRESENT_LOCK, PRESENT_WINDOWED };
    static const int OLD_PATTERN[] = {
        PAT_COLOR_BARS, PAT_RAMPS, PAT_GRID, PAT_PIXELS, PAT_GEOMETRY, PAT_DITHER, PAT_TEXT, PAT_MOTION,
        PAT_INTERLACED, PAT_PALETTE, PAT_WHITE, PAT_TEST_IMAGE, PAT_IMAGE,
    };
    char buf[32];

    GetPrivateProfileStringA(sec, "present", "", buf, sizeof(buf), g.ini_path);
    for (int i = 0; i < PRESENT_COUNT; i++)
        if (_stricmp(buf, PRESENT_SHORT[i]) == 0)
            s->present = i;
    if (buf[0] >= '0' && buf[0] <= '9' && atoi(buf) < (int)(sizeof(OLD_PRESENT) / sizeof(OLD_PRESENT[0])))
        s->present = OLD_PRESENT[atoi(buf)];

    GetPrivateProfileStringA(sec, "pattern", "", buf, sizeof(buf), g.ini_path);
    if (pattern_from_short_name(buf) >= 0)
        s->pattern = pattern_from_short_name(buf);
    if (buf[0] >= '0' && buf[0] <= '9' && atoi(buf) < (int)(sizeof(OLD_PATTERN) / sizeof(OLD_PATTERN[0])))
        s->pattern = OLD_PATTERN[atoi(buf)];

    s->info = GetPrivateProfileIntA(sec, "info", s->info, g.ini_path) != 0;
    s->mouse = GetPrivateProfileIntA(sec, "mouse", s->mouse, g.ini_path) != 0;
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
    write_int("ddraw", s->ddraw);
    WritePrivateProfileStringA("ddtest", "present", PRESENT_SHORT[s->present], g.ini_path);
    WritePrivateProfileStringA("ddtest", "pattern", pattern_short_name(s->pattern), g.ini_path);
    write_int("info", s->info);
    write_int("mouse", s->mouse);
    WritePrivateProfileStringA("ddtest", "image", s->image_path, g.ini_path);
}

static const char* USAGE =
    "ddtest options (all optional):\n\n"
    "  --size WxH, --width N, --height N\n"
    "  --bpp 8|16|24|32\n"
    "  --refresh HZ        (0 = default)\n"
    "  --fps N             frame rate limit (0 = none)\n"
    "  --ddraw 1|2|4|7     the IDirectDraw interface version (7)\n"
    "  --present flip|blt|vblank|lock|windowed\n"
    "  --pattern NAME|NUMBER\n"
    "      bars, ramps, grid, pixels, geometry, dither, text, motion,\n"
    "      interlaced, palette, fade, blits, white, testimage, image\n"
    "  --image FILE        image file for the image pattern (shown 1:1)\n"
    "  --info 0|1          the info text in the picture (I)\n"
    "  --mouse 0|1         the mouse crosshair (M)\n"
    "  --start             run straight away\n"
    "  --dump DIR          save the patterns as PNG files and exit\n"
    "  --frame N           animation frame for --dump\n"
    "  --frames N          with --start: exit after N frames\n"
    "                      (exit code 3: DirectDraw failed, 4: self-check failed)\n"
    "  --log FILE          with --frames: write the result to FILE\n";

static BOOL parse_args(Settings* s, BOOL* start_now, char* dump_dir, uint32_t* frame, int* only_pattern)
{
    for (int i = 1; i < __argc; i++)
    {
        const char* a = __argv[i];
        const char* v = (i + 1 < __argc) ? __argv[i + 1] : NULL;
        BOOL takes_value = TRUE;

        if (_stricmp(a, "--start") == 0) { *start_now = TRUE; takes_value = FALSE; }
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
        else if (_stricmp(a, "--ddraw") == 0)
        {
            s->ddraw = atoi(v);
            if (_stricmp(ddraw_name(s->ddraw), "?") == 0)
            {
                MessageBoxA(NULL, USAGE, "ddtest: --ddraw is 1, 2, 4 or 7", MB_OK | MB_ICONERROR);
                return FALSE;
            }
        }
        else if (_stricmp(a, "--info") == 0) { s->info = atoi(v) != 0; }
        else if (_stricmp(a, "--mouse") == 0) { s->mouse = atoi(v) != 0; }
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

        /* In 8-bit modes the fade is in the palette */
        if (p == PAT_FADE && s->bpp == 8)
        {
            int level = fade_level(frame);
            for (int i = 0; i < c->w * c->h; i++)
                out[i] = COL(COL_R(out[i]) * level / 256, COL_G(out[i]) * level / 256, COL_B(out[i]) * level / 256);
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

/* The panel is laid out from the font's measured size at the window's DPI, so
   it stays readable at any display scaling. (DirectCRT makes the process
   DPI-aware when it loads; Windows' own ddraw.dll doesn't, and then Windows
   scales the whole panel.) */

static const char* IMAGE_NOTE = "Shown 1:1, centred and cropped";

static int window_dpi(HWND hwnd)
{
    static UINT (WINAPI* get_dpi_for_window)(HWND);
    static BOOL looked_up;

    if (!looked_up)
    {
        looked_up = TRUE;
        get_dpi_for_window = (UINT (WINAPI*)(HWND))GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    }

    if (get_dpi_for_window && hwnd)
    {
        UINT dpi = get_dpi_for_window(hwnd);
        if (dpi)
            return (int)dpi;
    }

    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    return dpi > 0 ? dpi : 96;
}

static HFONT create_ui_font(int dpi)
{
    static BOOL (WINAPI* spi_for_dpi)(UINT, UINT, PVOID, UINT, UINT);
    static BOOL looked_up;

    if (!looked_up)
    {
        looked_up = TRUE;
        spi_for_dpi = (BOOL (WINAPI*)(UINT, UINT, PVOID, UINT, UINT))
            GetProcAddress(GetModuleHandleA("user32.dll"), "SystemParametersInfoForDpi");
    }

    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);

    if (spi_for_dpi && spi_for_dpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, (UINT)dpi))
        return CreateFontIndirectW(&ncm.lfMessageFont);

    /* Older Windows: the system DPI's metrics, scaled to this DPI */
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, dpi, window_dpi(NULL));
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

/* Every control starts a new group except radio buttons, so the arrow keys
   move between the radio buttons of one group and Tab leaves it. The first
   radio button of a group passes WS_GROUP itself. */
static HWND add_control(const char* cls, const char* text, DWORD style, int id)
{
    DWORD ex = (strcmp(cls, "EDIT") == 0) ? WS_EX_CLIENTEDGE : 0;
    BOOL radio = strcmp(cls, "BUTTON") == 0 && (style & BS_TYPEMASK) == BS_AUTORADIOBUTTON;
    if (!radio)
        style |= WS_GROUP;

    HWND hwnd = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                                g.panel, (HMENU)(INT_PTR)id, g.inst, NULL);
    SendMessageA(hwnd, WM_SETFONT, (WPARAM)g.font, FALSE);
    return hwnd;
}

static HWND add_label(const char* text)
{
    return add_control("STATIC", text, SS_LEFT | SS_NOPREFIX, -1);
}

static void create_controls(void)
{
    const DWORD edit = WS_TABSTOP | ES_AUTOHSCROLL;
    const DWORD number = edit | ES_NUMBER;
    const DWORD combo = WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST;
    const DWORD check = WS_TABSTOP | BS_AUTOCHECKBOX;
    const DWORD button = WS_TABSTOP | BS_PUSHBUTTON;
    const DWORD radio = BS_AUTORADIOBUTTON;
    const DWORD first_radio = radio | WS_GROUP | WS_TABSTOP;

    /* Created in tab order */
    /* The ddraw.dll a run loads; long paths lose their middle */
    g.lb_dll = add_label("ddraw.dll:");
    g.st_dll = add_control("STATIC", "", SS_LEFT | SS_NOPREFIX | SS_PATHELLIPSIS, -1);

    g.lb_size = add_label("Resolution:");
    g.cb_res = add_control("COMBOBOX", "", combo, ID_RES);
    for (int i = 0; i < RES_COUNT; i++)
    {
        char buf[32];
        _snprintf(buf, sizeof(buf) - 1, "%ld x %ld", RESOLUTIONS[i].cx, RESOLUTIONS[i].cy);
        buf[sizeof(buf) - 1] = '\0';
        ComboBox_AddString(g.cb_res, buf);
    }
    ComboBox_AddString(g.cb_res, "Custom");

    /* The size itself; picking from the list fills these in */
    g.ed_width = add_control("EDIT", "", number, ID_WIDTH);
    g.lb_x = add_label("x");
    g.ed_height = add_control("EDIT", "", number, ID_HEIGHT);

    g.lb_bpp = add_label("Bit depth:");
    for (int i = 0; i < BPP_COUNT; i++)
    {
        char buf[16];
        _snprintf(buf, sizeof(buf) - 1, "%d-bit", BPP_VALUES[i]);
        buf[sizeof(buf) - 1] = '\0';
        g.rb_bpp[i] = add_control("BUTTON", buf, i == 0 ? first_radio : radio, ID_BPP0 + i);
    }

    g.lb_refresh = add_label("Refresh rate:");
    g.ed_refresh = add_control("EDIT", "", number, ID_REFRESH);
    g.lb_hz = add_label("Hz  (0 = default)");

    g.lb_fps = add_label("Frame limit:");
    g.ed_fps = add_control("EDIT", "", number, ID_FPS);
    g.lb_fps_unit = add_label("fps  (0 = none; page flipping and vblank also wait for vsync)");

    g.lb_ddraw = add_label("Interface:");
    for (int i = 0; i < DDRAW_COUNT; i++)
        g.rb_ddraw[i] = add_control("BUTTON", DDRAW_NAMES[i], i == 0 ? first_radio : radio, ID_DDRAW0 + i);

    g.lb_present = add_label("Presentation:");
    for (int i = 0; i < PRESENT_COUNT; i++)
        g.rb_present[i] = add_control("BUTTON", PRESENT_NAMES[i], i == 0 ? first_radio : radio, ID_PRESENT0 + i);

    g.lb_pattern = add_label("Pattern:");
    g.cb_pattern = add_control("COMBOBOX", "", combo, ID_PATTERN);
    for (int i = 0; i < PAT_COUNT; i++)
        ComboBox_AddString(g.cb_pattern, pattern_name(i));
    g.chk_info = add_control("BUTTON", "Info text", check, ID_INFO);
    g.chk_mouse = add_control("BUTTON", "Crosshair", check, ID_MOUSE);

    g.lb_image = add_label("Custom image:");
    g.ed_image = add_control("EDIT", "", edit, ID_IMAGE);
    g.bt_browse = add_control("BUTTON", "Browse...", button, ID_BROWSE);
    g.bt_imgsize = add_control("BUTTON", "Use image size", button, ID_IMGSIZE);
    g.st_image_note = add_label(IMAGE_NOTE);

    g.lb_modes = add_label("DirectDraw modes:");
    g.cb_modes = add_control("COMBOBOX", "", combo, ID_MODES);
    SendMessageW(g.cb_modes, CB_SETCUEBANNER, 0, (LPARAM)L"Press List for the modes ddraw.dll offers");
    g.bt_query = add_control("BUTTON", "List", button, ID_QUERY);

    g.bt_run = add_control("BUTTON", "Run", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_RUN);
    g.bt_stop = add_control("BUTTON", "Stop", button, ID_STOP);

    /* The state, and on the right whatever went wrong; placed by layout_controls */
    g.sb_status = add_control(STATUSCLASSNAMEA, "", SBARS_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE, ID_STATUS);
}

static int text_w(HDC dc, const char* s)
{
    SIZE sz;
    GetTextExtentPoint32A(dc, s, (int)strlen(s), &sz);
    return sz.cx;
}

static int imax(int a, int b)
{
    return a > b ? a : b;
}

static void place(HWND h, int x, int y, int w, int hh)
{
    SetWindowPos(h, NULL, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Positions every control and returns the client size the panel needs */
static SIZE layout_controls(void)
{
    int dpi = g.dpi;
#define S(v) MulDiv((v), dpi, 96)

    HDC dc = GetDC(g.panel);
    HGDIOBJ old_font = SelectObject(dc, g.font);
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);

    int line = tm.tmHeight;
    int margin = S(12), gap = S(8), small_gap = S(5);

    /* Edits and buttons match the height Windows gives drop-down lists for this font */
    RECT rc;
    GetWindowRect(g.cb_res, &rc);
    int ch = imax(line + S(8), rc.bottom - rc.top);
    int row = ch + S(7);
    int text_dy = (ch - line) / 2;

    /* Single-line edits draw their text at the top, so they're only as tall as
       the text needs and centred in the row */
    int eh = line + S(7);
    int edy = (ch - eh) / 2;

    const char* labels[] = { "ddraw.dll:", "Resolution:", "Bit depth:", "Refresh rate:", "Frame limit:", "Interface:", "Presentation:",
                             "Pattern:", "Custom image:", "DirectDraw modes:" };
    int label_w = 0;
    for (int i = 0; i < (int)(sizeof(labels) / sizeof(labels[0])); i++)
        label_w = imax(label_w, text_w(dc, labels[i]));
    label_w += gap;

    int fx = margin + label_w;
    int system_dpi = window_dpi(NULL);
    int arrow = MulDiv(GetSystemMetrics(SM_CXVSCROLL), dpi, system_dpi) + S(12);
    int check_w = MulDiv(GetSystemMetrics(SM_CXMENUCHECK), dpi, system_dpi) + S(8);
    int num_w = text_w(dc, "00000") + S(14);
    int btn_pad = S(14);
    int btn_w = imax(imax(text_w(dc, "Browse..."), text_w(dc, "Query")), text_w(dc, "Run")) + 2 * btn_pad;

    int res_w = text_w(dc, "3840 x 2160") + arrow + S(12);
    int radio_gap = 3 * gap;
    int radio_h = imax(line, MulDiv(GetSystemMetrics(SM_CYMENUCHECK), dpi, system_dpi)) + S(6);

    int field_w = tm.tmAveCharWidth * 52;
    for (int i = 0; i < PRESENT_COUNT; i++)
        field_w = imax(field_w, check_w + text_w(dc, PRESENT_NAMES[i]));
    for (int i = 0; i < PAT_COUNT; i++)
        field_w = imax(field_w, text_w(dc, pattern_name(i)) + arrow + S(12));

    int ddraw_row_w = (DDRAW_COUNT - 1) * 2 * gap;
    for (int i = 0; i < DDRAW_COUNT; i++)
        ddraw_row_w += check_w + text_w(dc, DDRAW_NAMES[i]);
    field_w = imax(field_w, ddraw_row_w);

    int full_w = fx + field_w - margin;
    int y = margin;

    place(g.lb_dll, margin, y + text_dy, label_w, line);
    place(g.st_dll, fx, y + text_dy, field_w, line);
    y += row;

    /* Resolution, and the custom size next to it */
    place(g.lb_size, margin, y + text_dy, label_w, line);
    int x = fx;
    place(g.cb_res, x, y, res_w, ch + line * (RES_COUNT + 2));
    x += res_w + 2 * gap;
    place(g.ed_width, x, y + edy, num_w, eh);
    x += num_w + small_gap;
    place(g.lb_x, x, y + text_dy, text_w(dc, "x"), line);
    x += text_w(dc, "x") + small_gap;
    place(g.ed_height, x, y + edy, num_w, eh);
    y += row;

    place(g.lb_bpp, margin, y + text_dy, label_w, line);
    x = fx;
    for (int i = 0; i < BPP_COUNT; i++)
    {
        char buf[16];
        GetWindowTextA(g.rb_bpp[i], buf, sizeof(buf));
        int w = check_w + text_w(dc, buf);
        place(g.rb_bpp[i], x, y + (ch - radio_h) / 2, w, radio_h);
        x += w + radio_gap;
    }
    y += row;

    place(g.lb_refresh, margin, y + text_dy, label_w, line);
    place(g.ed_refresh, fx, y + edy, num_w, eh);
    place(g.lb_hz, fx + num_w + gap, y + text_dy, field_w - num_w - gap, line);
    y += row;

    place(g.lb_fps, margin, y + text_dy, label_w, line);
    place(g.ed_fps, fx, y + edy, num_w, eh);
    place(g.lb_fps_unit, fx + num_w + gap, y + text_dy, field_w - num_w - gap, line);
    y += row;

    place(g.lb_ddraw, margin, y + text_dy, label_w, line);
    x = fx;
    for (int i = 0; i < DDRAW_COUNT; i++)
    {
        int w = check_w + text_w(dc, DDRAW_NAMES[i]);
        place(g.rb_ddraw[i], x, y + (ch - radio_h) / 2, w, radio_h);
        x += w + 2 * gap;
    }
    y += row;

    place(g.lb_present, margin, y + (radio_h - line) / 2, label_w, line);
    for (int i = 0; i < PRESENT_COUNT; i++)
    {
        place(g.rb_present[i], fx, y, field_w, radio_h);
        y += radio_h;
    }
    y += S(10);

    place(g.lb_pattern, margin, y + text_dy, label_w, line);
    place(g.cb_pattern, fx, y, field_w, ch + line * (PAT_COUNT + 1));
    y += row;

    int info_w = check_w + text_w(dc, "Info text");
    place(g.chk_info, fx, y, info_w, ch);
    place(g.chk_mouse, fx + info_w + 3 * gap, y, check_w + text_w(dc, "Crosshair"), ch);
    y += row;

    place(g.lb_image, margin, y + text_dy, label_w, line);
    place(g.ed_image, fx, y + edy, field_w - btn_w - gap, eh);
    place(g.bt_browse, fx + field_w - btn_w, y, btn_w, ch);
    y += row;

    int size_w = text_w(dc, "Use image size") + 2 * btn_pad;
    place(g.bt_imgsize, fx, y, size_w, ch);
    place(g.st_image_note, fx + size_w + gap, y + text_dy, field_w - size_w - gap, line);
    y += row;

    place(g.lb_modes, margin, y + text_dy, label_w, line);
    place(g.cb_modes, fx, y, field_w - btn_w - gap, ch + line * 16);
    place(g.bt_query, fx + field_w - btn_w, y, btn_w, ch);
    y += row + S(6);

    int big_w = btn_w + S(16), big_h = ch + S(6);
    place(g.bt_run, fx, y, big_w, big_h);
    place(g.bt_stop, fx + big_w + gap, y, big_w, big_h);
    y += big_h + margin;

    /* The status bar across the bottom */
    int status_h = line + S(8);
    place(g.sb_status, 0, y, fx + field_w + margin, status_h);
    y += status_h;

    SelectObject(dc, old_font);
    ReleaseDC(g.panel, dc);

    SIZE size = { fx + field_w + margin, y };
    return size;
#undef S
}

/* Sizes the panel to fit its controls; pos is the top-left corner to use, or NULL to keep it */
static void layout_panel(const POINT* pos)
{
    static BOOL (WINAPI* adjust_for_dpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static BOOL looked_up;

    if (!looked_up)
    {
        looked_up = TRUE;
        adjust_for_dpi = (BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT))
            GetProcAddress(GetModuleHandleA("user32.dll"), "AdjustWindowRectExForDpi");
    }

    SIZE client = layout_controls();

    DWORD style = (DWORD)GetWindowLongA(g.panel, GWL_STYLE);
    DWORD ex_style = (DWORD)GetWindowLongA(g.panel, GWL_EXSTYLE);
    RECT r = { 0, 0, client.cx, client.cy };

    if (adjust_for_dpi)
        adjust_for_dpi(&r, style, FALSE, ex_style, (UINT)g.dpi);
    else
        AdjustWindowRectEx(&r, style, FALSE, ex_style);

    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | (pos ? 0 : SWP_NOMOVE);
    SetWindowPos(g.panel, NULL, pos ? pos->x : 0, pos ? pos->y : 0, r.right - r.left, r.bottom - r.top, flags);
    InvalidateRect(g.panel, NULL, TRUE);
}

static void panel_dpi_changed(int dpi, const RECT* suggested)
{
    g.dpi = dpi;

    HFONT old = g.font;
    g.font = create_ui_font(dpi);
    for (HWND h = GetWindow(g.panel, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        SendMessageA(h, WM_SETFONT, (WPARAM)g.font, FALSE);
    if (old)
        DeleteObject(old);

    POINT pos = { suggested->left, suggested->top };
    layout_panel(&pos);
}

static void set_int_text(HWND edit, int value)
{
    char buf[16];
    _snprintf(buf, sizeof(buf) - 1, "%d", value);
    buf[sizeof(buf) - 1] = '\0';
    SetWindowTextA(edit, buf);
}

/* The width and height boxes hold the size; the list shows the matching
   entry, or "Custom" for any other size */
static void sync_resolution_list(void)
{
    char buf[32];
    GetWindowTextA(g.ed_width, buf, sizeof(buf));
    long w = strtol(buf, NULL, 10);
    GetWindowTextA(g.ed_height, buf, sizeof(buf));
    long h = strtol(buf, NULL, 10);

    int index = RES_CUSTOM;
    for (int i = 0; i < RES_COUNT; i++)
        if (RESOLUTIONS[i].cx == w && RESOLUTIONS[i].cy == h)
            index = i;

    if (ComboBox_GetCurSel(g.cb_res) != index)
        ComboBox_SetCurSel(g.cb_res, index);
}

static void set_resolution(int w, int h)
{
    set_int_text(g.ed_width, w);
    set_int_text(g.ed_height, h);
    sync_resolution_list();
}

static void set_bpp(int bpp)
{
    for (int i = 0; i < BPP_COUNT; i++)
        Button_SetCheck(g.rb_bpp[i], BPP_VALUES[i] == bpp ? BST_CHECKED : BST_UNCHECKED);
}

static void write_panel(const Settings* s)
{
    set_resolution(s->width, s->height);
    set_bpp(s->bpp);
    set_int_text(g.ed_refresh, s->refresh);
    set_int_text(g.ed_fps, s->fps);

    for (int i = 0; i < DDRAW_COUNT; i++)
        Button_SetCheck(g.rb_ddraw[i], DDRAW_VERSIONS[i] == s->ddraw ? BST_CHECKED : BST_UNCHECKED);
    for (int i = 0; i < PRESENT_COUNT; i++)
        Button_SetCheck(g.rb_present[i], i == s->present ? BST_CHECKED : BST_UNCHECKED);
    ComboBox_SetCurSel(g.cb_pattern, s->pattern);
    Button_SetCheck(g.chk_info, s->info ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(g.chk_mouse, s->mouse ? BST_CHECKED : BST_UNCHECKED);
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

static int panel_ddraw_version(void)
{
    for (int i = 0; i < DDRAW_COUNT; i++)
        if (Button_GetCheck(g.rb_ddraw[i]) == BST_CHECKED)
            return DDRAW_VERSIONS[i];
    return 7;
}

static BOOL read_panel(Settings* s)
{
    if (!read_int(g.ed_width, "Width", 1, MAX_DIMENSION, &s->width)) return FALSE;
    if (!read_int(g.ed_height, "Height", 1, MAX_DIMENSION, &s->height)) return FALSE;

    if (!read_int(g.ed_refresh, "Refresh rate", 0, 1000, &s->refresh)) return FALSE;
    if (!read_int(g.ed_fps, "Frame limit", 0, 10000, &s->fps)) return FALSE;

    s->bpp = BPP_VALUES[0];
    for (int i = 0; i < BPP_COUNT; i++)
        if (Button_GetCheck(g.rb_bpp[i]) == BST_CHECKED)
            s->bpp = BPP_VALUES[i];

    s->ddraw = panel_ddraw_version();

    s->present = PRESENT_FLIP;
    for (int i = 0; i < PRESENT_COUNT; i++)
        if (Button_GetCheck(g.rb_present[i]) == BST_CHECKED)
            s->present = i;

    s->pattern = ComboBox_GetCurSel(g.cb_pattern);
    if (s->pattern < 0) s->pattern = PAT_COLOR_BARS;

    s->info = Button_GetCheck(g.chk_info) == BST_CHECKED;
    s->mouse = Button_GetCheck(g.chk_mouse) == BST_CHECKED;
    GetWindowTextA(g.ed_image, s->image_path, sizeof(s->image_path));
    return TRUE;
}

static void update_status(void)
{
    if (!g.sb_status || !IsWindow(g.sb_status))
        return;

    /* Run while stopped, Stop while running */
    if (IsWindowEnabled(g.bt_run) == g.running)
        EnableWindow(g.bt_run, !g.running);
    if (IsWindowEnabled(g.bt_stop) != g.running)
        EnableWindow(g.bt_stop, g.running);

    /* The state on the left; on the right the last error, or the self-check's failure */
    char state[256];
    char problem[sizeof(g.error) + sizeof(g.check_msg) + 64] = "";
    if (g.running)
    {
        int n = _snprintf(state, sizeof(state) - 1, "  Running: %d x %d, %d-bit, %s, %s - %.1f fps",
                          g.cur.width, g.cur.height, g.cur.bpp, PRESENT_DESC[g.cur.present],
                          ddraw_name(g.cur.ddraw), g.fps);
        if (n > 0 && g.cur.pattern == PAT_BLITS && g.check_frames && !g.check_failed)
            _snprintf(state + n, sizeof(state) - 1 - n, " - self-check passed");
    }
    else
        _snprintf(state, sizeof(state) - 1, "  Stopped");
    state[sizeof(state) - 1] = '\0';

    if (g.error[0])
        _snprintf(problem, sizeof(problem) - 1, "  Error: %s", g.error);
    else if (g.running && g.cur.pattern == PAT_BLITS && g.check_failed)
        _snprintf(problem, sizeof(problem) - 1, "  Self-check failed in %u of %u frames: %s",
                  g.check_failed, g.check_frames, g.check_msg);
    problem[sizeof(problem) - 1] = '\0';

    /* One part for the state alone, two with a problem: the state as wide as its text.
       The texts start with two spaces: a status bar puts its text against the edge. */
    int parts[2] = { -1, -1 };
    int count = 1;
    if (problem[0])
    {
        HDC dc = GetDC(g.sb_status);
        HGDIOBJ old_font = SelectObject(dc, g.font);
        SIZE sz;
        GetTextExtentPoint32A(dc, state, (int)strlen(state), &sz);
        SelectObject(dc, old_font);
        ReleaseDC(g.sb_status, dc);

        RECT rc;
        GetClientRect(g.sb_status, &rc);
        parts[0] = sz.cx + MulDiv(24, g.dpi, 96);
        if (parts[0] > rc.right * 2 / 3)
            parts[0] = rc.right * 2 / 3;
        count = 2;
    }

    int old_parts[2] = { 0, 0 };
    int old_count = (int)SendMessageA(g.sb_status, SB_GETPARTS, 2, (LPARAM)old_parts);
    if (old_count != count || old_parts[0] != parts[0])
        SendMessageA(g.sb_status, SB_SETPARTS, count, (LPARAM)parts);

    char old[sizeof(problem)];
    const char* texts[2] = { state, problem };
    for (int i = 0; i < count; i++)
    {
        old[0] = '\0';
        if (LOWORD(SendMessageA(g.sb_status, SB_GETTEXTLENGTHA, i, 0)) < sizeof(old))
            SendMessageA(g.sb_status, SB_GETTEXTA, i, (LPARAM)old);
        if (old_count != count || strcmp(old, texts[i]) != 0)
        {
            SendMessageA(g.sb_status, SB_SETTEXTA, i, (LPARAM)texts[i]);
            /* Shown when the text doesn't fit */
            SendMessageA(g.sb_status, SB_SETTIPTEXTA, i, (LPARAM)texts[i]);
        }
    }

    const char* dll = g.ddraw_path[0] ? g.ddraw_path : "(not loaded)";
    GetWindowTextA(g.st_dll, old, sizeof(old));
    if (strcmp(old, dll) != 0)
        SetWindowTextA(g.st_dll, dll);
}

/* Run starts a test with the panel's settings. The button is disabled during
   a run, so every run starts fresh: a new test window and a newly loaded
   ddraw.dll, like a game being started. */
static void run(void)
{
    if (g.running)
        return;

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

    g.running = TRUE;
    SetForegroundWindow(g.display);
    SetFocus(g.display);
    update_status();
}

/* Applies the panel to a running test, the way a game changes its settings.
   The pattern, overlays, frame limit and image just change. Fullscreen, a new
   resolution, bit depth or refresh rate, or another fullscreen presentation,
   is a mode change on the same DirectDraw object; windowed, so is a new bit
   depth. Switching between windowed and fullscreen, a new windowed size or
   another interface version starts over as a fresh run. When stopped, the
   panel is only read on Run. */
static void apply_live(void)
{
    if (!g.running)
        return;

    Settings s;
    g.error[0] = '\0';

    if (!read_panel(&s))
    {
        update_status();
        return;
    }

    if (!g.quit_after_frames)
        settings_save(&s);

    if (strcmp(s.image_path, g.image_loaded_path) != 0)
        load_image_file(s.image_path);

    BOOL was_windowed = g.cur.present == PRESENT_WINDOWED;
    BOOL windowed = s.present == PRESENT_WINDOWED;
    BOOL mode_change = s.width != g.cur.width || s.height != g.cur.height || s.bpp != g.cur.bpp ||
                       s.present != g.cur.present || s.ddraw != g.cur.ddraw ||
                       (!windowed && s.refresh != g.cur.refresh);

    if (!mode_change)
    {
        g.cur = s;
        update_status();
        return;
    }

    /* A wrapper may take a windowed program's window size as its mode, once,
       when the program sets its cooperative level (DirectCRT does), so a new
       windowed size starts over too */
    BOOL new_window_size = windowed && (s.width != g.cur.width || s.height != g.cur.height);

    /* Windowed, the panel keeps the focus so you can go on changing things;
       fullscreen, the test window needs it */
    HWND focus = GetFocus();
    if (!focus || !IsChild(g.panel, focus))
        focus = NULL;

    if (windowed != was_windowed || new_window_size || s.ddraw != g.cur.ddraw)
    {
        g.restart_focus = windowed ? focus : NULL;
        stop();
        g.restart_pending = TRUE;
        return;
    }

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

    if (windowed && focus)
    {
        SetForegroundWindow(g.panel);
        SetFocus(focus);
    }

    update_status();
}

static BOOL is_text_box(HWND h)
{
    return h && (h == g.ed_width || h == g.ed_height || h == g.ed_refresh || h == g.ed_fps || h == g.ed_image);
}

/* A text box lost the focus. The custom width and height count as one
   field, so going from one to the other doesn't change the mode twice. */
static void commit_text_box(HWND from)
{
    HWND to = GetFocus();
    BOOL size_box = from == g.ed_width || from == g.ed_height;
    if (size_box && (to == g.ed_width || to == g.ed_height))
        return;

    apply_live();
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
    {
        SetWindowTextA(g.ed_image, path);
        apply_live();
    }
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
        set_resolution(g.image_w, g.image_h);
        apply_live();
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

    /* Through the interface the panel has: the modes may differ by version */
    if (!dd)
    {
        if (!dd_create(panel_ddraw_version(), &dd))
        {
            update_status();
            return;
        }
        temporary = TRUE;
    }

    g.mode_count = 0;
    HRESULT hr = IDirectDraw7_EnumDisplayModes(dd, DDEDM_REFRESHRATES, NULL, NULL, enum_modes_cb);
    dd_ok(hr, "EnumDisplayModes");

    /* Unload again when stopped, so the next Run still gets a fresh ddraw.dll */
    if (temporary)
        IDirectDraw7_Release(dd);
    if (!g.running)
        g.unload_pending = TRUE;

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
    set_resolution(m->w, m->h);
    set_int_text(g.ed_refresh, m->hz);
    set_bpp(m->bpp);
    apply_live();
}

/* Moves the picture of an image pattern, as far as its edges */
static void pan_image(int dx, int dy)
{
    int iw = 0, ih = 0;
    if (g.cur.pattern == PAT_TEST_IMAGE)
        testimage_get(&iw, &ih);
    else if (g.cur.pattern == PAT_IMAGE && g.image)
        iw = g.image_w, ih = g.image_h;
    else
        return;

    g.pan_x += dx;
    g.pan_y += dy;
    image_pan_clamp(iw, ih, g.cur.width, g.cur.height, &g.pan_x, &g.pan_y);
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
        g.dpi = window_dpi(hwnd);
        g.font = create_ui_font(g.dpi);
        create_controls();
        layout_panel(NULL);
        SetTimer(hwnd, TIMER_STATUS, 500, NULL);
        return 0;

    case 0x02E0: /* WM_DPICHANGED: moved to a monitor with a different scale */
        panel_dpi_changed(HIWORD(wp), (const RECT*)lp);
        return 0;

    case WM_TIMER:
        update_status();
        return 0;

    case WM_APP_RESIZED:
        if (g.running)
        {
            set_resolution((int)wp, (int)lp);
            apply_live();
        }
        return 0;

    case WM_APP_COMMIT:
        commit_text_box((HWND)lp);
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wp), code = HIWORD(wp);

        if (id >= ID_BPP0 && id < ID_BPP0 + BPP_COUNT && code == BN_CLICKED)
        {
            apply_live();
            return 0;
        }

        if (id >= ID_PRESENT0 && id < ID_PRESENT0 + PRESENT_COUNT && code == BN_CLICKED)
        {
            apply_live();
            return 0;
        }

        if (id >= ID_DDRAW0 && id < ID_DDRAW0 + DDRAW_COUNT && code == BN_CLICKED)
        {
            apply_live();
            return 0;
        }

        switch (id)
        {
        case ID_RUN: g.pan_x = g.pan_y = 0; g.have_window_pos = FALSE; run(); return 0;
        case ID_STOP: g.error[0] = '\0'; stop(); return 0;
        case ID_BROWSE: browse_image(); return 0;
        case ID_IMGSIZE: use_image_size(); return 0;
        case ID_QUERY: query_modes(); return 0;

        case ID_MODES:
            if (code == CBN_SELCHANGE)
                use_mode(ComboBox_GetCurSel(g.cb_modes));
            return 0;

        case ID_RES:
            if (code == CBN_SELCHANGE)
            {
                int res = ComboBox_GetCurSel(g.cb_res);
                if (res == RES_CUSTOM)
                {
                    /* Type a size into the boxes */
                    SetFocus(g.ed_width);
                    Edit_SetSel(g.ed_width, 0, -1);
                }
                else if (res >= 0)
                {
                    set_resolution((int)RESOLUTIONS[res].cx, (int)RESOLUTIONS[res].cy);
                    apply_live();
                }
            }
            return 0;

        /* Text boxes apply when you press Enter or leave them, not on every
           key; the resolution list follows the size as it's typed */
        case ID_WIDTH: case ID_HEIGHT: case ID_REFRESH: case ID_FPS: case ID_IMAGE:
            if (code == EN_CHANGE && (id == ID_WIDTH || id == ID_HEIGHT))
                sync_resolution_list();
            else if (code == EN_KILLFOCUS)
                PostMessageA(hwnd, WM_APP_COMMIT, 0, (LPARAM)lp);
            return 0;

        /* These never need anything else from the panel */
        case ID_PATTERN:
            if (code == CBN_SELCHANGE && g.running)
                g.cur.pattern = ComboBox_GetCurSel(g.cb_pattern);
            return 0;
        case ID_INFO:
            if (g.running) g.cur.info = Button_GetCheck(g.chk_info) == BST_CHECKED;
            return 0;
        case ID_MOUSE:
            if (g.running) g.cur.mouse = Button_GetCheck(g.chk_mouse) == BST_CHECKED;
            return 0;
        }
        break;
    }

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
        case VK_LEFT:
        case VK_UP:
        case VK_DOWN:
            if (GetKeyState(VK_CONTROL) < 0)
            {
                /* Move the picture: 32 pixels a press, 1 with Shift */
                int step = GetKeyState(VK_SHIFT) < 0 ? 1 : 32;
                pan_image(wp == VK_RIGHT ? step : wp == VK_LEFT ? -step : 0,
                          wp == VK_DOWN ? step : wp == VK_UP ? -step : 0);
            }
            else if (wp == VK_RIGHT || wp == VK_LEFT)
            {
                change_pattern(wp == VK_RIGHT ? 1 : -1);
            }
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

    case WM_EXITSIZEMOVE:
        /* The user resized the window: a windowed program shows its window
           1:1, so the new size is the new resolution. GetWindowInfo gives
           the real size; a wrapper may answer GetClientRect with the mode. */
        if (g.running && g.cur.present == PRESENT_WINDOWED)
        {
            WINDOWINFO wi;
            ZeroMemory(&wi, sizeof(wi));
            wi.cbSize = sizeof(wi);

            if (GetWindowInfo(hwnd, &wi))
            {
                int w = wi.rcClient.right - wi.rcClient.left;
                int h = wi.rcClient.bottom - wi.rcClient.top;

                if (w > 0 && h > 0 && (w != g.cur.width || h != g.cur.height))
                {
                    g.window_pos.x = wi.rcWindow.left;
                    g.window_pos.y = wi.rcWindow.top;
                    g.have_window_pos = TRUE;
                    PostMessageA(g.panel, WM_APP_RESIZED, (WPARAM)w, (LPARAM)h);
                }
            }
        }
        break;

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
    if (_stricmp(ddraw_name(s->ddraw), "?") == 0) s->ddraw = 7;
    if (s->present < 0 || s->present >= PRESENT_COUNT) s->present = PRESENT_FLIP;
    if (s->pattern < 0 || s->pattern >= PAT_COUNT) s->pattern = PAT_COLOR_BARS;
}

static void write_run_log(void)
{
    if (!g.log_path[0])
        return;

    FILE* f = fopen(g.log_path, "w");
    if (!f)
        return;

    if (g.running)
    {
        /* The average over the whole run: a short run without a frame limit
           can end before the half-second rolling figure is first measured */
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double secs = (double)(now.QuadPart - g.run_t0.QuadPart) / (double)g.qpf.QuadPart;
        fprintf(f, "ok: %u frames, %d x %d, %d-bit, %s, ddraw %d, %.1f fps\nddraw.dll: %s\n", g.frame,
                g.cur.width, g.cur.height, g.cur.bpp, PRESENT_SHORT[g.cur.present], g.cur.ddraw,
                secs > 0 ? g.frame / secs : 0.0, g.ddraw_path);

        if (g.check_failed)
            fprintf(f, "check: failed in %u of %u frames; first: %s\n", g.check_failed, g.check_frames, g.check_msg);
        else if (g.check_frames)
            fprintf(f, "check: passed, %u frames\n", g.check_frames);
    }
    else
        fprintf(f, "error: %s\nddraw.dll: %s\n", g.error[0] ? g.error : "stopped", g.ddraw_path);

    fclose(f);
}

enum { STEP_CONTINUE, STEP_QUIT, STEP_FRAMES_DONE };

/* One pass of the main loop. It's kept out of line, and unloading ddraw.dll
   is the last thing it does, because a wrapper like DirectCRT patches this
   program's imports (PeekMessageA and others) while it's loaded: a loop that
   kept such an address in a register would go on calling the unloaded DLL. */
static __declspec(noinline) int main_loop_step(MSG* msg)
{
    /* Switching between windowed and fullscreen during a run: start over
       with a freshly loaded ddraw.dll, as a new game would. The last pass
       unloaded the old one. */
    if (g.restart_pending)
    {
        g.restart_pending = FALSE;
        run();

        if (g.running && g.restart_focus)
        {
            SetForegroundWindow(g.panel);
            SetFocus(g.restart_focus);
        }
        g.restart_focus = NULL;
    }

    if (g.quit_after_frames && (!g.running || g.frame >= g.quit_after_frames))
        return STEP_FRAMES_DONE;

    while (PeekMessageA(msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg->message == WM_QUIT)
            return STEP_QUIT;

        /* Enter in a text box applies it to a running test, or runs one */
        if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN && is_text_box(msg->hwnd))
        {
            if (g.running)
                apply_live();
            else
                run();
            continue;
        }

        if (!IsDialogMessageA(g.panel, msg))
        {
            TranslateMessage(msg);
            DispatchMessageA(msg);
        }
    }

    if (g.running)
    {
        render_frame();
        wait_for_next_frame();
    }
    else if (!g.unload_pending)
    {
        WaitMessage();
    }

    if (g.unload_pending)
    {
        g.unload_pending = FALSE;
        if (!g.running && !g.dd)
            unload_ddraw();
    }

    return STEP_CONTINUE;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)prev;
    (void)cmdline;

    g.inst = inst;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
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

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    /* Each size drawn for itself: the title bar's from the small one */
    wc.hIcon = (HICON)LoadImageA(inst, MAKEINTRESOURCEA(IDI_DDTEST), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageA(inst, MAKEINTRESOURCEA(IDI_DDTEST), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "DDTestPanel";
    RegisterClassExA(&wc);

    wc.lpfnWndProc = display_proc;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "DDTestDisplay";
    RegisterClassExA(&wc);

    /* The panel sizes itself to its contents when it's created */
    CreateWindowExA(0, "DDTestPanel", "ddtest - DirectDraw test patterns",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                    CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, NULL, NULL, inst, NULL);

    if (!g.panel)
        return 1;

    write_panel(&s);
    update_status();
    ShowWindow(g.panel, show);
    UpdateWindow(g.panel);

    if (start_now)
        run();

    int exit_code = 0;
    MSG msg;
    msg.wParam = 0;

    for (;;)
    {
        int step = main_loop_step(&msg);
        if (step == STEP_QUIT)
            break;

        if (step == STEP_FRAMES_DONE)
        {
            exit_code = !g.running ? 3 : g.check_failed ? 4 : 0;
            write_run_log();
            break;
        }
    }

    stop();
    free(g.image);
    testimage_free();
    free(g.canvas.px);
    free(g.canvas.idx);
    cv_shutdown();
    if (g.font)
        DeleteObject(g.font);
    timeEndPeriod(1);
    CoUninitialize();
    return g.quit_after_frames ? exit_code : (int)msg.wParam;
}
