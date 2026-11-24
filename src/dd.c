#include <windows.h>
#include <math.h>
#include "ddraw.h"
#include "IDirectDraw.h"
#include "dd.h"
#include "hook.h"
#include "config.h"
#include "mouse.h"
#include "keyboard.h"
#include "wndproc.h"
#include "render_d3d11.h"
#include "render_blank.h"
#include "render_ogl.h"
#include "fps_limiter.h"
#include "debug.h"
#include "utils.h"
#include "blt.h"
#include "versionhelpers.h"
#include "dllmain.h"
#include "directinput.h"
#include "opengl_utils.h"
#include "vbeam.h"
#include "overlay.h"


CNCDDRAW g_ddraw;

/* The pixel format DirectCRT gives a mode of this bit depth */
static void dd_mode_pixel_format(DDPIXELFORMAT* pf, DWORD bpp)
{
    memset(pf, 0, sizeof(DDPIXELFORMAT));

    pf->dwSize = sizeof(DDPIXELFORMAT);
    pf->dwFlags = DDPF_RGB;
    pf->dwRGBBitCount = bpp;

    if (bpp == 8)
    {
        pf->dwFlags |= DDPF_PALETTEINDEXED8;
    }
    else if (bpp == 16)
    {
        pf->dwRBitMask = 0xF800;
        pf->dwGBitMask = 0x07E0;
        pf->dwBBitMask = 0x001F;
    }
    else
    {
        pf->dwRBitMask = 0xFF0000;
        pf->dwGBitMask = 0x00FF00;
        pf->dwBBitMask = 0x0000FF;
    }
}

/* Passes the mode in s at this bit depth to the game, unless the game asked for
   another depth. FALSE when the enumeration stops */
static BOOL dd_enum_mode(
    DDSURFACEDESC2* s,
    DWORD bpp,
    DWORD bpp_filter,
    DWORD* res_count,
    LPVOID lpContext,
    LPDDENUMMODESCALLBACK lpEnumModesCallback)
{
    if (bpp_filter && bpp != bpp_filter)
        return TRUE;

    dd_mode_pixel_format(&s->ddpfPixelFormat, bpp);
    s->lPitch = ((s->dwWidth * bpp + 63) & ~63) >> 3;

    if (g_config.max_resolutions && (*res_count)++ >= g_config.max_resolutions)
    {
        TRACE("     resolution limit reached, stopping\n");
        return FALSE;
    }

    if (lpEnumModesCallback((LPDDSURFACEDESC)s, lpContext) == DDENUMRET_CANCEL)
    {
        TRACE("     DDENUMRET_CANCEL returned, stopping\n");
        return FALSE;
    }

    return TRUE;
}

static void dd_mode_desc(DDSURFACEDESC2* s, DWORD width, DWORD height)
{
    memset(s, 0, sizeof(*s));

    s->dwSize = sizeof(DDSURFACEDESC);
    s->dwFlags = DDSD_HEIGHT | DDSD_REFRESHRATE | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;
    s->dwRefreshRate = vbeam_rate();
    s->dwWidth = width;
    s->dwHeight = height;
}

HRESULT dd_EnumDisplayModes(
    DWORD dwFlags,
    LPDDSURFACEDESC lpDDSurfaceDesc,
    LPVOID lpContext,
    LPDDENUMMODESCALLBACK lpEnumModesCallback)
{
    dbg_dump_edm_flags(dwFlags);

    DDSURFACEDESC2 s = { 0 };
    DWORD bpp_filter = 0;

    if (lpDDSurfaceDesc)
    {
        dbg_dump_dds_flags(lpDDSurfaceDesc->dwFlags);
        dbg_dump_dds_caps(lpDDSurfaceDesc->ddsCaps.dwCaps);

        if (lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT)
        {
            TRACE("     ddpfPixelFormat.dwRGBBitCount=%u\n", lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount);

            switch (lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount)
            {
            case 8:
            case 16:
            case 24:
            case 32:
                bpp_filter = lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount;
                break;
            }

            if ((lpDDSurfaceDesc->dwFlags & DDSD_WIDTH) && (lpDDSurfaceDesc->dwFlags & DDSD_HEIGHT))
            {
                TRACE("     dwWidth=%u, dwHeight=%u\n", lpDDSurfaceDesc->dwWidth, lpDDSurfaceDesc->dwHeight);

                dd_mode_desc(&s, lpDDSurfaceDesc->dwWidth, lpDDSurfaceDesc->dwHeight);

                DWORD res_count = 0;
                dd_enum_mode(&s, bpp_filter ? bpp_filter : 8, 0, &res_count, lpContext, lpEnumModesCallback);
                return DD_OK;
            }
        }
    }

    DWORD i = 0;
    DWORD res_count = 0;

    /* Some games crash when you feed them with too many resolutions so we have to keep the list short */

    DWORD max_w = 0;
    DWORD max_h = 0;
    DEVMODE reg_m;

    memset(&reg_m, 0, sizeof(DEVMODE));
    reg_m.dmSize = sizeof(DEVMODE);

    if (real_EnumDisplaySettingsA(NULL, ENUM_REGISTRY_SETTINGS, &reg_m))
    {
        max_w = reg_m.dmPelsWidth;
        max_h = reg_m.dmPelsHeight;

        TRACE("     max_w=%u, max_h=%u\n", reg_m.dmPelsWidth, reg_m.dmPelsHeight);
    }

    BOOL rlf = g_config.resolutions == RESLIST_FULL;
    BOOL rlm = g_config.resolutions == RESLIST_MINI;

    SIZE resolutions[] =
    {
        { 320, 200 },
        { 320, 240 },
        { rlm ? 0 : 512, rlm ? 0 : 384 },
        { 640, 400 },
        { 640, 480 },
        { 800, 600 },
        { 1024, 768 },
        { 1280, 1024 },
        { rlm ? 0 : 1600, rlm ? 0 : 1200 },
        { 1280, 720 },
        { rlf ? 1024 : 0, rlf ? 600 : 0 },
        /* 4:3 */
        { rlf ? 1280 : 0, rlf ? 960 : 0 },
        { rlf ? 2048 : 0, rlf ? 1536 : 0 },
        /* 16:10 */
        { rlf ? 960 : 0, rlf ? 600 : 0 },
        { rlf ? 1440 : 0, rlf ? 900 : 0 },
        { rlf ? 1680 : 0, rlf ? 1050 : 0 },
        { rlf ? 1920 : 0, rlf ? 1200 : 0 },
        { rlf ? 2560 : 0, rlf ? 1600 : 0 },
        /* 16:9 */
        { rlf ? 960 : 0, rlf ? 540 : 0 },
        { rlf ? 1360 : 0, rlf ? 768 : 0 },
        { rlf ? 1600 : 0, rlf ? 900 : 0 },
        { rlf ? 1920 : 0, rlf ? 1080 : 0 },
        { rlf ? 2560 : 0, rlf ? 1440 : 0 },
        /* 21:9 */
        { rlf ? 1280 : 0, rlf ? 540 : 0 },
        { rlf ? 1720 : 0, rlf ? 720 : 0 },
        { rlf ? 2560 : 0, rlf ? 1080 : 0 },
        { max_w, max_h },
    };
    
    for (int x = 0; x < (sizeof(resolutions) / sizeof(resolutions[0])) - 1; x++)
    {
        if (resolutions[x].cx == max_w && resolutions[x].cy == max_h)
        {
            resolutions[x].cx = 0;
            resolutions[x].cy = 0;
        }
    }

    /* 24-bit modes only when the game asks for them, is in one, or with
       resolutions=full: games that take the first or second mode of a size
       shouldn't get a different one than before */
    static const DWORD depths[] = { 8, 16, 24, 32 };

    if (((g_ddraw.bpp && !g_ddraw.windowed_hack) && g_config.resolutions == RESLIST_NORMAL) ||
        g_config.resolutions == RESLIST_FULL)
    {
        TRACE("     g_ddraw.bpp=%u\n", g_ddraw.bpp);

        /* set up some filters to keep the list short */
        DWORD refresh_rate = 0;
        DWORD bpp = 0;
        DWORD flags = 99998;
        DWORD fixed_output = 99998;
        DEVMODE m;

        memset(&m, 0, sizeof(DEVMODE));
        m.dmSize = sizeof(DEVMODE);

        while (real_EnumDisplaySettingsA(NULL, i, &m))
        {
            TRACE_EXT(
                "     %u: %ux%u@%u %u bpp | flags=0x%08X, FO=%u\n",
                i,
                m.dmPelsWidth,
                m.dmPelsHeight,
                m.dmDisplayFrequency,
                m.dmBitsPerPel,
                m.dmDisplayFlags,
                m.dmDisplayFixedOutput);

            if (refresh_rate != 60 && refresh_rate < 120 && m.dmDisplayFrequency >= 50)
                refresh_rate = m.dmDisplayFrequency;

            /* Some setups with 144hz monitors only contain a very few 60hz resolutions so we can't use 60hz as filter */
            if (m.dmDisplayFrequency > refresh_rate && m.dmDisplayFrequency >= 120)
                refresh_rate = m.dmDisplayFrequency;

            if (bpp != 32 && m.dmBitsPerPel >= 16)
                bpp = m.dmBitsPerPel;

            if (flags != 0)
                flags = m.dmDisplayFlags;

            if (fixed_output != DMDFO_DEFAULT)
                fixed_output = m.dmDisplayFixedOutput;

            memset(&m, 0, sizeof(DEVMODE));
            m.dmSize = sizeof(DEVMODE);
            i++;
        }

        memset(&m, 0, sizeof(DEVMODE));
        m.dmSize = sizeof(DEVMODE);
        i = 0;

        while (real_EnumDisplaySettingsA(NULL, i, &m))
        {
            if (refresh_rate == m.dmDisplayFrequency &&
                bpp == m.dmBitsPerPel &&
                flags == m.dmDisplayFlags &&
                fixed_output == m.dmDisplayFixedOutput)
            {
                TRACE(
                    "     %u: %ux%u@%u %u bpp\n",
                    i,
                    m.dmPelsWidth,
                    m.dmPelsHeight,
                    m.dmDisplayFrequency,
                    m.dmBitsPerPel);

                dd_mode_desc(&s, m.dmPelsWidth, m.dmPelsHeight);

                for (int d = 0; d < sizeof(depths) / sizeof(depths[0]); d++)
                {
                    if (g_ddraw.bpp == depths[d] || g_config.resolutions == RESLIST_FULL)
                    {
                        if (!dd_enum_mode(&s, depths[d], bpp_filter, &res_count, lpContext, lpEnumModesCallback))
                            return DD_OK;
                    }
                }

                for (int x = 0; x < sizeof(resolutions) / sizeof(resolutions[0]); x++)
                {
                    if (resolutions[x].cx == m.dmPelsWidth && resolutions[x].cy == m.dmPelsHeight)
                    {
                        resolutions[x].cx = 0;
                        resolutions[x].cy = 0;
                    }
                }
            }

            memset(&m, 0, sizeof(DEVMODE));
            m.dmSize = sizeof(DEVMODE);
            i++;
        }
    }

    if (!g_ddraw.bpp || g_config.resolutions != RESLIST_NORMAL || g_ddraw.windowed_hack)
    {
        for (i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++)
        {
            if (!resolutions[i].cx || !resolutions[i].cy)
                continue;

            if ((max_w && resolutions[i].cx > max_w) || (max_h && resolutions[i].cy > max_h))
            {
                DEVMODE m;
                memset(&m, 0, sizeof(DEVMODE));

                m.dmSize = sizeof(DEVMODE);
                m.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
                m.dmPelsWidth = resolutions[i].cx;
                m.dmPelsHeight = resolutions[i].cy;
            
                if (ChangeDisplaySettings(&m, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
                {
                    TRACE("     SKIPPED: %ux%u\n", m.dmPelsWidth, m.dmPelsHeight);
                    continue;
                }
            }

            dd_mode_desc(&s, resolutions[i].cx, resolutions[i].cy);

            for (int d = 0; d < sizeof(depths) / sizeof(depths[0]); d++)
            {
                if (depths[d] == 24 && bpp_filter != 24 && g_config.resolutions != RESLIST_FULL)
                    continue;

                if (depths[d] == 32 && g_config.resolutions == RESLIST_MINI)
                    continue;

                if (!dd_enum_mode(&s, depths[d], bpp_filter, &res_count, lpContext, lpEnumModesCallback))
                    return DD_OK;
            }
        }
    }

    return DD_OK;
}

HRESULT dd_GetCaps(LPDDCAPS_DX1 lpDDDriverCaps, LPDDCAPS_DX1 lpDDEmulCaps)
{
    if (lpDDDriverCaps)
    {
        int size =
            lpDDDriverCaps->dwSize == sizeof(DDCAPS_DX3) ? sizeof(DDCAPS_DX3) : 
            lpDDDriverCaps->dwSize == sizeof(DDCAPS_DX5) ? sizeof(DDCAPS_DX5) : 
            lpDDDriverCaps->dwSize == sizeof(DDCAPS_DX6) ? sizeof(DDCAPS_DX6) : 
            lpDDDriverCaps->dwSize == sizeof(DDCAPS_DX7) ? sizeof(DDCAPS_DX7) : 
            sizeof(DDCAPS_DX1);

        memset(lpDDDriverCaps, 0, size);

        lpDDDriverCaps->dwSize = size;
        lpDDDriverCaps->dwCaps =
            DDCAPS_BLT | 
            DDCAPS_PALETTE | 
            DDCAPS_BLTCOLORFILL | 
            DDCAPS_BLTSTRETCH | 
            DDCAPS_CANCLIP | 
            DDCAPS_CANBLTSYSMEM |
            DDCAPS_CANCLIPSTRETCHED | 
            DDCAPS_COLORKEY;

        lpDDDriverCaps->dwCaps2 =
            DDCAPS2_NOPAGELOCKREQUIRED |
            DDCAPS2_WIDESURFACES;

        lpDDDriverCaps->dwCKeyCaps = 
            DDCKEYCAPS_SRCBLT | 
            DDCKEYCAPS_SRCBLTCLRSPACE;
        
        lpDDDriverCaps->dwFXCaps =
            DDFXCAPS_BLTMIRRORLEFTRIGHT |
            DDFXCAPS_BLTMIRRORUPDOWN;

        lpDDDriverCaps->dwPalCaps = 
            DDPCAPS_8BIT | 
            DDPCAPS_PRIMARYSURFACE;

        lpDDDriverCaps->dwVidMemTotal = 16777216;
        lpDDDriverCaps->dwVidMemFree = 16777216;

        lpDDDriverCaps->ddsCaps.dwCaps = 
            DDSCAPS_BACKBUFFER | 
            DDSCAPS_COMPLEX | 
            DDSCAPS_FLIP | 
            DDSCAPS_FRONTBUFFER | 
            DDSCAPS_OFFSCREENPLAIN | 
            DDSCAPS_PRIMARYSURFACE | 
            DDSCAPS_VIDEOMEMORY | 
            DDSCAPS_OWNDC | 
            DDSCAPS_LOCALVIDMEM | 
            DDSCAPS_NONLOCALVIDMEM;
    }

    if (lpDDEmulCaps)
    {
        int size =
            lpDDEmulCaps->dwSize == sizeof(DDCAPS_DX3) ? sizeof(DDCAPS_DX3) :
            lpDDEmulCaps->dwSize == sizeof(DDCAPS_DX5) ? sizeof(DDCAPS_DX5) :
            lpDDEmulCaps->dwSize == sizeof(DDCAPS_DX6) ? sizeof(DDCAPS_DX6) :
            lpDDEmulCaps->dwSize == sizeof(DDCAPS_DX7) ? sizeof(DDCAPS_DX7) :
            sizeof(DDCAPS_DX1);

        memset(lpDDEmulCaps, 0, size);

        lpDDEmulCaps->dwSize = size;
        lpDDEmulCaps->dwCaps = DDCAPS_BLTSTRETCH;
    }

    return DD_OK;
}

HRESULT dd_GetDisplayMode(LPDDSURFACEDESC lpDDSurfaceDesc)
{
    if (lpDDSurfaceDesc)
    {
        int size = lpDDSurfaceDesc->dwSize == sizeof(DDSURFACEDESC2) ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC);

        memset(lpDDSurfaceDesc, 0, size);

        unsigned long width = 1024;
        unsigned long height = 768;
        unsigned long bpp = 16;

        if (g_ddraw.width)
        {
            width = g_ddraw.width;
            height = g_ddraw.height;
            bpp = g_ddraw.bpp;
        }
        else if (g_config.fake_mode[0])
        {
            char* e = &g_config.fake_mode[0];

            width = strtoul(e, &e, 0);
            height = strtoul(e + 1, &e, 0);
            bpp = strtoul(e + 1, &e, 0);
        }

        lpDDSurfaceDesc->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = bpp;

        lpDDSurfaceDesc->dwSize = size;
        lpDDSurfaceDesc->dwFlags = DDSD_HEIGHT | DDSD_REFRESHRATE | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;
        lpDDSurfaceDesc->dwRefreshRate = vbeam_rate();
        lpDDSurfaceDesc->dwWidth = width;
        lpDDSurfaceDesc->dwHeight = height;

        if (bpp == 32 || bpp == 24)
        {
            lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = DDPF_RGB;
            lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0xFF0000;
            lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x00FF00;
            lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x0000FF;
        }
        else if (bpp == 8)
        {
            lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
        }
        else
        {
            lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = DDPF_RGB;
            lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = 16;
            lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0xF800;
            lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x07E0;
            lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x001F;
        }

        lpDDSurfaceDesc->lPitch = 
            ((lpDDSurfaceDesc->dwWidth * lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount + 63) & ~63) >> 3;
    }

    return DD_OK;
}

void dd_set_refresh(DWORD game_hz)
{
    DWORD hz = g_config.emulated_refresh ? g_config.emulated_refresh : game_hz;

    g_ddraw.refresh_asked = game_hz;

    /* A game that doesn't ask, or asks for a rate no CRT had, gets 60 Hz */
    vbeam_set_rate(hz >= 24 && hz <= 240 ? hz : 60);
    TRACE("     emulated refresh %lu Hz (the game asked for %lu)\n", vbeam_rate(), game_hz);
}

HRESULT dd_GetMonitorFrequency(LPDWORD lpdwFreq)
{
    if (lpdwFreq)
        *lpdwFreq = vbeam_rate();

    return DD_OK;
}

HRESULT dd_GetVerticalBlankStatus(LPBOOL lpbIsInVB)
{
    if (!lpbIsInVB)
        return DDERR_INVALIDPARAMS;

    *lpbIsInVB = vbeam_in_vblank();

    TRACE("     lpbIsInVB = %s\n", *lpbIsInVB ? "TRUE" : "FALSE");

    return DD_OK;
}

HRESULT dd_GetScanLine(LPDWORD lpdwScanLine)
{
    if (!lpdwScanLine)
        return DDERR_INVALIDPARAMS;

    BOOL in_vblank;
    *lpdwScanLine = vbeam_scanline(&in_vblank);

    TRACE_EXT("     lpdwScanLine = %u%s\n", *lpdwScanLine, in_vblank ? " (vertical blank)" : "");

    return in_vblank ? DDERR_VERTICALBLANKINPROGRESS : DD_OK;
}

HRESULT dd_RestoreDisplayMode()
{
    if (!g_ddraw.render.run)
    {
        return DD_OK;
    }

    EnterCriticalSection(&g_ddraw.cs);
    g_ddraw.render.run = FALSE;
    ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
    LeaveCriticalSection(&g_ddraw.cs);

    if (g_ddraw.render.thread)
    {
        WaitForSingleObject(g_ddraw.render.thread, INFINITE);
        g_ddraw.render.thread = NULL;
    }

    if (!g_config.windowed)
    {
        ChangeDisplaySettings(NULL, 0);
    }

    //real_ShowWindow(g_ddraw.hwnd, SW_MINIMIZE);

    return DD_OK;
}

/* The desktop and its display scale, for crt-auto's 'viewport' sizes */
static CRT_AUTO_DISPLAY dd_crt_auto_display()
{
    CRT_AUTO_DISPLAY display = { 0 };
    display.desktop_width = g_ddraw.mode.dmPelsWidth;
    display.desktop_height = g_ddraw.mode.dmPelsHeight;

    UINT dpi = 0;

    /* Windows 10 1607 and later */
    typedef UINT(WINAPI* GETDPIFORWINDOWPROC)(HWND);
    GETDPIFORWINDOWPROC get_dpi_for_window =
        (GETDPIFORWINDOWPROC)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");

    if (get_dpi_for_window && g_ddraw.hwnd)
        dpi = get_dpi_for_window(g_ddraw.hwnd);

    if (!dpi)
    {
        HDC dc = GetDC(NULL);

        if (dc)
        {
            dpi = GetDeviceCaps(dc, LOGPIXELSX);
            ReleaseDC(NULL, dc);
        }
    }

    display.scale = dpi ? dpi / 96.0f : 1.0f;

    return display;
}

/* Stops the render thread, for a renderer to be set up again */
static void dd_stop_render_thread(void)
{
    if (!g_ddraw.render.thread)
        return;

    EnterCriticalSection(&g_ddraw.cs);
    g_ddraw.render.run = FALSE;
    ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
    LeaveCriticalSection(&g_ddraw.cs);

    WaitForSingleObject(g_ddraw.render.thread, INFINITE);
    g_ddraw.render.thread = NULL;
}

static DISPLAY_KIND dd_display_kind(void)
{
    return g_config.windowed ? DISPLAY_KIND_WINDOW :
        g_config.fullscreen ? DISPLAY_KIND_DESKTOP : DISPLAY_KIND_MODE_SWITCH;
}

typedef DWORD(WINAPI* DD_RENDERER)(void);

static DD_RENDERER dd_next_renderer(void);

/* Whether renderer=opengl waits for the game to start again: Direct3D 11
 * keeps going in the window it has shown its frames in (dd_next_renderer) */
BOOL dd_opengl_waits(void)
{
    return _strcmpi(g_config.renderer, "opengl") == 0 && g_ddraw.renderer == d3d11_render_main &&
        d3d11_flipped(g_ddraw.hwnd);
}

/* To the renderer the settings give now, the render thread stopped */
static void dd_switch_renderer(void)
{
    DD_RENDERER next = dd_next_renderer();

    if (!g_ddraw.renderer || next == g_ddraw.renderer)
        return;

    if (g_ddraw.renderer == ogl_render_main)
    {
        ogl_release();
    }
    else if (g_ddraw.renderer == d3d11_render_main)
    {
        d3d11_release();
    }

    g_ddraw.renderer = next;
}

/* Where the picture goes in the canvas_width x canvas_height window or
 * screen, and its shader (crt_auto_calc_layout), from the settings; and the
 * mouse's scale to it */
static void dd_set_picture(int canvas_width, int canvas_height)
{
    g_ddraw.render.viewport.width = canvas_width;
    g_ddraw.render.viewport.height = canvas_height;
    g_ddraw.render.viewport.x = 0;
    g_ddraw.render.viewport.y = 0;

    {
        /* As in DOSBox Staging: the picture's size from the aspect, viewport
         * and integer_scaling settings, and with shader=crt-auto the shader
         * from the screen pixels per game line */
        CRT_AUTO_DISPLAY display = dd_crt_auto_display();

        g_ddraw.render.crt =
            crt_auto_calc_layout(
                &g_config.crt,
                &display,
                g_ddraw.width,
                g_ddraw.height,
                canvas_width,
                canvas_height);

        if (g_ddraw.render.crt.viewport_rejected)
        {
            TRACE(
                "     the viewport is larger than the %dx%d desktop (scale %.2f), using fit\n",
                display.desktop_width,
                display.desktop_height,
                display.scale);
        }

        g_ddraw.render.viewport.x = g_ddraw.render.crt.x;
        g_ddraw.render.viewport.y = g_ddraw.render.crt.y;
        g_ddraw.render.viewport.width = g_ddraw.render.crt.width;
        g_ddraw.render.viewport.height = g_ddraw.render.crt.height;

        TRACE(
            "     picture: %ux%u in %dx%d: %s, %d px/line, render %dx%d, picture %d,%d %dx%d\n",
            g_ddraw.width,
            g_ddraw.height,
            canvas_width,
            canvas_height,
            crt_auto_shader_name(g_ddraw.render.crt.shader),
            g_ddraw.render.crt.pixels_per_scanline,
            g_ddraw.render.crt.render_width,
            g_ddraw.render.crt.render_height,
            g_ddraw.render.crt.x,
            g_ddraw.render.crt.y,
            g_ddraw.render.crt.width,
            g_ddraw.render.crt.height);
    }

    g_ddraw.render.scale_w = ((float)g_ddraw.render.viewport.width / g_ddraw.width);
    g_ddraw.render.scale_h = ((float)g_ddraw.render.viewport.height / g_ddraw.height);
    g_ddraw.render.unscale_w = ((float)g_ddraw.width / g_ddraw.render.viewport.width);
    g_ddraw.render.unscale_h = ((float)g_ddraw.height / g_ddraw.render.viewport.height);

    /* Hack for games that require the cursor to be in the exact center of the screen (Worms 2 / Atlantis) */
    if (g_config.center_cursor_fix)
    {
        g_ddraw.mouse.scale_x = ((float)(g_ddraw.render.viewport.width) / (g_ddraw.width));
        g_ddraw.mouse.scale_y = ((float)(g_ddraw.render.viewport.height) / (g_ddraw.height));
        g_ddraw.mouse.unscale_x = ((float)(g_ddraw.width) / (g_ddraw.render.viewport.width));
        g_ddraw.mouse.unscale_y = ((float)(g_ddraw.height) / (g_ddraw.render.viewport.height));
    }
    else
    {
        g_ddraw.mouse.scale_x = ((float)(g_ddraw.render.viewport.width - 1) / (g_ddraw.width - 1));
        g_ddraw.mouse.scale_y = ((float)(g_ddraw.render.viewport.height - 1) / (g_ddraw.height - 1));
        g_ddraw.mouse.unscale_x = ((float)(g_ddraw.width - 1) / (g_ddraw.render.viewport.width - 1));
        g_ddraw.mouse.unscale_y = ((float)(g_ddraw.height - 1) / (g_ddraw.render.viewport.height - 1));
    }

    g_ddraw.mouse.x_adjust = g_ddraw.render.viewport.x;
    g_ddraw.mouse.y_adjust = g_ddraw.render.viewport.y;

    if (g_config.lock_mouse_top_left)
    {
        g_ddraw.mouse.x_adjust = 0;
        g_ddraw.mouse.y_adjust = 0;
    }

    g_ddraw.mouse.rc.left = g_ddraw.mouse.x_adjust;
    g_ddraw.mouse.rc.top = g_ddraw.mouse.y_adjust;
    g_ddraw.mouse.rc.right = g_ddraw.width + g_ddraw.mouse.x_adjust;
    g_ddraw.mouse.rc.bottom = g_ddraw.height + g_ddraw.mouse.y_adjust;

    if (g_config.adjmouse)
    {
        g_ddraw.mouse.rc.right = g_ddraw.render.viewport.width + g_ddraw.mouse.x_adjust;
        g_ddraw.mouse.rc.bottom = g_ddraw.render.viewport.height + g_ddraw.mouse.y_adjust;
    }

    {
        /* With 'viewport=relative' the picture can be larger than the window;
         * keep the cursor in the window */
        g_ddraw.mouse.rc.left = max(g_ddraw.mouse.rc.left, 0);
        g_ddraw.mouse.rc.top = max(g_ddraw.mouse.rc.top, 0);
        g_ddraw.mouse.rc.right = min(g_ddraw.mouse.rc.right, (LONG)canvas_width);
        g_ddraw.mouse.rc.bottom = min(g_ddraw.mouse.rc.bottom, (LONG)canvas_height);
    }
}

/* Live reload of the settings: called from a timer on the game's window
 * (now = FALSE), and when the settings OSD has written directcrt.ini
 * (now = TRUE). What the picture is made with (the shader settings, the
 * window, the frame rate, the renderer) sets it up again, as Alt+Enter; a new
 * display goes to the window or fullscreen it gives */
void dd_reload_settings(BOOL now)
{
    CRT_AUTO_SETTINGS crt;
    DWORD refresh = g_config.emulated_refresh;
    BOOL status_osd = g_config.status_osd;
    DISPLAY_MODE display = g_config.display;
    WINDOW_SIZE window_size = g_config.window_size;
    WINDOW_POSITION window_position = g_config.window_position;
    BOOL decorations = g_config.window_decorations;
    BOOL resizable = g_config.window_resizable;
    int maxfps = g_config.maxfps;
    BOOL vsync = g_config.vsync;
    BOOL adjmouse = g_config.adjmouse;
    char renderer[sizeof(g_config.renderer)];
    memcpy(renderer, g_config.renderer, sizeof(renderer));

    if (!cfg_reload_settings(&crt, now))
        return;

    if (g_config.status_osd != status_osd)
        overlay_set_status(g_config.status_osd);

    if (g_config.emulated_refresh != refresh && g_ddraw.width)
        dd_set_refresh(g_ddraw.refresh_asked);

    BOOL size_changed = memcmp(&window_size, &g_config.window_size, sizeof(window_size)) != 0;
    BOOL position_changed = memcmp(&window_position, &g_config.window_position, sizeof(window_position)) != 0;

    /* A new size or place, over where the user dragged the window */
    if (size_changed)
        g_config.window_rect.right = g_config.window_rect.bottom = 0;

    if (position_changed)
        g_config.window_rect.left = g_config.window_rect.top = -32000;

    /* battle.net keeps the window as it is until it's done */
    if (g_ddraw.bnet_active)
    {
        g_ddraw.bnet_was_resizable = g_config.window_resizable;
        g_config.window_resizable = resizable;
    }

    BOOL window_changed = size_changed || position_changed ||
        decorations != g_config.window_decorations || resizable != g_config.window_resizable;
    /* Only when another one would run: OpenGL can wait (dd_opengl_waits) */
    BOOL renderer_changed = _stricmp(renderer, g_config.renderer) != 0 &&
        (!g_ddraw.renderer || dd_next_renderer() != g_ddraw.renderer);
    BOOL picture_changed = memcmp(&crt, &g_config.crt, sizeof(crt)) != 0 || window_changed ||
        maxfps != g_config.maxfps || vsync != g_config.vsync || adjmouse != g_config.adjmouse || renderer_changed;

    /* What 'display' gives now, for what the game asked for */
    DISPLAY_KIND kind = ds_display_start(g_config.display, g_ddraw.saw_fullscreen || !g_ddraw.saw_normal);
    BOOL display_changed = display != g_config.display && kind != dd_display_kind();

    if (!picture_changed && !display_changed)
    {
        TRACE("     settings: nothing to set up again\n");
        return;
    }

    /* Only the picture: the render thread keeps going and sets up its shader
       passes again between two frames. Stopping it would hold the screen
       still a while, the settings OSD with it */
    BOOL only_picture = !display_changed && !window_changed && !renderer_changed &&
        maxfps == g_config.maxfps && vsync == g_config.vsync && adjmouse == g_config.adjmouse;

    if (only_picture && g_ddraw.render.thread && g_ddraw.width && !g_ddraw.bnet_active &&
        (g_ddraw.renderer == d3d11_render_main || g_ddraw.renderer == ogl_render_main))
    {
        TRACE("     settings: a new picture, set up while it runs\n");

        BOOL relock = g_mouse_locked;

        if (relock)
            mouse_unlock();

        EnterCriticalSection(&g_ddraw.cs);
        g_config.crt = crt;
        dd_set_picture(g_ddraw.render.width, g_ddraw.render.height - g_ddraw.render.opengl_y_align);
        InterlockedExchange(&g_ddraw.render.picture_changed, TRUE);
        InterlockedExchange(&g_ddraw.render.surface_updated, TRUE);
        LeaveCriticalSection(&g_ddraw.cs);

        ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);

        if (relock)
            mouse_lock();

        return;
    }

    if (!g_ddraw.render.thread || !g_ddraw.width || g_ddraw.bnet_active)
    {
        /* For the next display mode */
        g_config.crt = crt;

        if (renderer_changed && !g_ddraw.render.thread)
            dd_switch_renderer();

        if (display_changed && !g_ddraw.width)
        {
            g_config.windowed = kind == DISPLAY_KIND_WINDOW;
            g_config.fullscreen = kind == DISPLAY_KIND_DESKTOP;
        }

        return;
    }

    TRACE("     settings: new ones, setting up the picture again\n");

    /* The renderer reads its settings while it starts */
    dd_stop_render_thread();
    g_config.crt = crt;

    if (renderer_changed)
        dd_switch_renderer();

    if (display_changed)
    {
        /* To the window, and on to the fullscreen it gives (Alt+Enter); between
         * two fullscreens by way of the window */
        if (dd_display_kind() != DISPLAY_KIND_WINDOW)
            util_toggle_fullscreen();

        if (kind != DISPLAY_KIND_WINDOW && dd_display_kind() == DISPLAY_KIND_WINDOW)
            util_toggle_fullscreen();

        return;
    }

    dd_SetDisplayMode(0, 0, 0, 0);
}

/* Where a window's client area of width x height goes, on the desktop. In
 * order: where the user moved it, window_position, or (auto) centred where
 * the window was last or where the game put its own window, else on the
 * screen. A window keeps its centre when its size changes. *clamp is set when
 * the window should be kept on the screen */
static POINT dd_window_origin(int width, int height, BOOL* clamp)
{
    POINT p;
    POINT centre;

    *clamp = FALSE;

    if (g_config.window_rect.left != -32000 && g_config.window_rect.top != -32000)
    {
        /* The user's place, with the size it had then */
        int cw = g_config.window_rect.right > 0 ? g_config.window_rect.right :
            g_ddraw.window.placed ? g_ddraw.window.size.cx : width;
        int ch = g_config.window_rect.bottom > 0 ? g_config.window_rect.bottom :
            g_ddraw.window.placed ? g_ddraw.window.size.cy : height;

        centre.x = g_config.window_rect.left + cw / 2;
        centre.y = g_config.window_rect.top + ch / 2;
    }
    else if (!g_config.window_position.is_auto)
    {
        CRT_AUTO_DISPLAY display = dd_crt_auto_display();
        int x, y;

        ds_window_position_pixels(
            &g_config.window_position,
            display.desktop_width,
            display.desktop_height,
            display.scale,
            &x,
            &y);

        p.x = x;
        p.y = y;
        return p;
    }
    else if (g_ddraw.window.placed)
    {
        centre = g_ddraw.window.centre;
        *clamp = TRUE;
    }
    else
    {
        centre.x = (g_ddraw.mode.dmPelsWidth ? g_ddraw.mode.dmPelsWidth : width) / 2;
        centre.y = (g_ddraw.mode.dmPelsHeight ? g_ddraw.mode.dmPelsHeight : height) / 2;
        *clamp = TRUE;
    }

    p.x = centre.x - width / 2;
    p.y = centre.y - height / 2;
    return p;
}

/* Keep a window (its outer rect) in its monitor's work area, with the title
 * bar visible when it's larger */
static void dd_keep_on_screen(RECT* rc)
{
    MONITORINFO mi = { sizeof(mi) };
    HMONITOR monitor = MonitorFromRect(rc, MONITOR_DEFAULTTONEAREST);

    if (!monitor || !GetMonitorInfoA(monitor, &mi))
        return;

    RECT work = mi.rcWork;
    LONG dx = 0, dy = 0;

    if (rc->right > work.right)
        dx = work.right - rc->right;

    if (rc->left + dx < work.left)
        dx = work.left - rc->left;

    if (rc->bottom > work.bottom)
        dy = work.bottom - rc->bottom;

    if (rc->top + dy < work.top)
        dy = work.top - rc->top;

    OffsetRect(rc, dx, dy);
}

HRESULT dd_SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwFlags)
{
    if (!dwWidth)
        dwWidth = g_ddraw.width ? g_ddraw.width : 800;

    if (!dwHeight)
        dwHeight = g_ddraw.height ? g_ddraw.height : 600;

    if (!dwBPP)
        dwBPP = g_ddraw.bpp ? g_ddraw.bpp : 16;

    if (dwBPP != 8 && dwBPP != 16 && dwBPP != 24 && dwBPP != 32)
        return DDERR_INVALIDMODE;

    if (g_ddraw.render.thread)
    {
        EnterCriticalSection(&g_ddraw.cs);
        g_ddraw.render.run = FALSE;
        ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
        LeaveCriticalSection(&g_ddraw.cs);

        WaitForSingleObject(g_ddraw.render.thread, INFINITE);
        g_ddraw.render.thread = NULL;
    }

    if (!g_ddraw.mode.dmPelsWidth)
    {
        ChangeDisplaySettings(NULL, 0);

        g_ddraw.mode.dmSize = sizeof(DEVMODE);
        g_ddraw.mode.dmDriverExtra = 0;

        if (real_EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &g_ddraw.mode) == FALSE)
        {
            g_ddraw.mode.dmSize = sizeof(DEVMODE);
            g_ddraw.mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
            g_ddraw.mode.dmPelsWidth = real_GetSystemMetrics(SM_CXSCREEN);
            g_ddraw.mode.dmPelsHeight = real_GetSystemMetrics(SM_CYSCREEN);
            g_ddraw.mode.dmDisplayFrequency = 60;
            g_ddraw.mode.dmBitsPerPel = 32;

            if (!g_ddraw.mode.dmPelsWidth || !g_ddraw.mode.dmPelsHeight)
            {
                g_config.fullscreen = FALSE;
            }
        }
    }

    BOOL zooming = g_ddraw.zoom.enabled;
    g_ddraw.zoom.enabled = FALSE;

    /* An 'unscaled' window follows the game's resolution, also after the user
       resized it */
    if (g_config.window_size.unscaled &&
        g_ddraw.width &&
        (g_ddraw.width != dwWidth || g_ddraw.height != dwHeight) &&
        g_config.window_rect.right > 0 &&
        g_config.window_rect.bottom > 0)
    {
        g_ddraw.window.size.cx = g_config.window_rect.right;
        g_ddraw.window.size.cy = g_config.window_rect.bottom;
        g_config.window_rect.right = 0;
        g_config.window_rect.bottom = 0;
    }

    g_ddraw.width = dwWidth;
    g_ddraw.height = dwHeight;
    vbeam_set_lines(dwHeight);
    g_ddraw.bpp = dwBPP;

    /* A window's client size: the user's, or window_size */
    if (g_config.window_rect.right > 0 && g_config.window_rect.bottom > 0)
    {
        g_ddraw.render.width = g_config.window_rect.right;
        g_ddraw.render.height = g_config.window_rect.bottom;
    }
    else
    {
        CRT_AUTO_DISPLAY display = dd_crt_auto_display();

        ds_window_client_size(
            &g_config.window_size,
            display.desktop_width,
            display.desktop_height,
            display.scale,
            dwWidth,
            dwHeight,
            &g_ddraw.render.width,
            &g_ddraw.render.height);
    }

    InterlockedExchange((LONG*)&g_ddraw.cursor.x, dwWidth / 2);
    InterlockedExchange((LONG*)&g_ddraw.cursor.y, dwHeight / 2);

    BOOL border = g_config.window_decorations;
    BOOL nonexclusive = FALSE;

    if (g_config.fullscreen)
    {
        g_ddraw.render.width = g_ddraw.mode.dmPelsWidth;
        g_ddraw.render.height = g_ddraw.mode.dmPelsHeight;

        if (g_config.windowed) /* windowed-fullscreen aka borderless */
        {
            border = FALSE;

            if (!g_config.remove_menu && GetMenu(g_ddraw.hwnd))
            {
                g_ddraw.render.height -= real_GetSystemMetrics(SM_CYMENU);
            }

            /* prevent OpenGL from going automatically into fullscreen exclusive mode */
            if (g_ddraw.renderer == ogl_render_main)
                nonexclusive = TRUE;

        }
    }
    else if (zooming)
    {
        if (g_ddraw.width > g_ddraw.mode.dmPelsWidth ||
            g_ddraw.height > g_ddraw.mode.dmPelsHeight)
        {
            /* Downscaling requires adjmouse to be enabled */
            g_config.adjmouse = TRUE;
        }

        /* Do not change display resolution when zooming */
        g_ddraw.render.width = g_ddraw.render.mode.dmPelsWidth;
        g_ddraw.render.height = g_ddraw.render.mode.dmPelsHeight;

        /* Resize and alt+enter are not supported yet with zooming */
        g_config.window_resizable = FALSE;
        g_config.hotkeys.toggle_fullscreen = 0;
    }
    else if (!g_config.windowed)
    {
        /* display=original: the monitor switches to the game's resolution, or
           the closest mode it has (below) */
        g_ddraw.render.width = g_ddraw.width;
        g_ddraw.render.height = g_ddraw.height;
    }

    /* Fullscreen is never smaller than the game's resolution, except when
       borderless (below). A window can be: the picture is scaled down */
    if ((!zooming || g_config.fullscreen) && !g_config.windowed)
    {
        if (g_ddraw.render.width < g_ddraw.width)
        {
            g_ddraw.render.width = g_ddraw.width;
        }

        if (g_ddraw.render.height < g_ddraw.height)
        {
            g_ddraw.render.height = g_ddraw.height;
        }
    }

    g_ddraw.render.run = TRUE;

    BOOL lock_mouse = g_mouse_locked;

    mouse_unlock();

    memset(&g_ddraw.render.mode, 0, sizeof(DEVMODE));
    g_ddraw.render.mode.dmSize = sizeof(DEVMODE);

    g_ddraw.render.mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    g_ddraw.render.mode.dmPelsWidth = g_ddraw.render.width;
    g_ddraw.render.mode.dmPelsHeight = g_ddraw.render.height;

    if (g_config.refresh_rate)
    {
        g_ddraw.render.mode.dmFields |= DM_DISPLAYFREQUENCY;
        g_ddraw.render.mode.dmDisplayFrequency = g_config.refresh_rate;
        
        if (ChangeDisplaySettings(&g_ddraw.render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
        {
            g_config.refresh_rate = 0;

            g_ddraw.render.mode.dmFields &= ~DM_DISPLAYFREQUENCY;
            g_ddraw.render.mode.dmDisplayFrequency = 0;
        }
    }

    if (!g_config.windowed)
    {
        /* Making sure the chosen resolution is valid */
        if (ChangeDisplaySettings(&g_ddraw.render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
        {
            /* Try without upscaling */
            g_ddraw.render.width = g_ddraw.width;
            g_ddraw.render.height = g_ddraw.height;

            g_ddraw.render.mode.dmPelsWidth = g_ddraw.render.width;
            g_ddraw.render.mode.dmPelsHeight = g_ddraw.render.height;

            if (ChangeDisplaySettings(&g_ddraw.render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
            {
                /* Try 2x scaling */
                g_ddraw.render.width *= 2;
                g_ddraw.render.height *= 2;

                g_ddraw.render.mode.dmPelsWidth = g_ddraw.render.width;
                g_ddraw.render.mode.dmPelsHeight = g_ddraw.render.height;

                if ((g_ddraw.render.width > g_ddraw.mode.dmPelsWidth ||
                    g_ddraw.render.height > g_ddraw.mode.dmPelsHeight) ||
                    ChangeDisplaySettings(&g_ddraw.render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
                {
                    SIZE res = { 0 };

                    /* try to get a resolution with the same aspect ratio as the requested resolution */
                    BOOL found_res = util_get_lowest_resolution(
                        (float)g_ddraw.width / g_ddraw.height,
                        &res,
                        g_ddraw.width + 1, /* don't return the original resolution since we tested that one already */
                        g_ddraw.height,
                        g_ddraw.mode.dmPelsWidth,
                        g_ddraw.mode.dmPelsHeight);

                    if (!found_res)
                    {
                        /* try to get a resolution with the same aspect ratio as the current display mode */
                        found_res = util_get_lowest_resolution(
                            (float)g_ddraw.mode.dmPelsWidth / g_ddraw.mode.dmPelsHeight,
                            &res,
                            g_ddraw.width,
                            g_ddraw.height,
                            g_ddraw.mode.dmPelsWidth,
                            g_ddraw.mode.dmPelsHeight);
                    }

                    g_ddraw.render.width = res.cx;
                    g_ddraw.render.height = res.cy;

                    g_ddraw.render.mode.dmPelsWidth = g_ddraw.render.width;
                    g_ddraw.render.mode.dmPelsHeight = g_ddraw.render.height;

                    if (!found_res ||
                        ChangeDisplaySettings(&g_ddraw.render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
                    {
                        if (g_ddraw.width > g_ddraw.mode.dmPelsWidth ||
                            g_ddraw.height > g_ddraw.mode.dmPelsHeight)
                        {
                            /* Downscaling requires adjmouse to be enabled */
                            g_config.adjmouse = TRUE;
                        }

                        /* try current display settings */
                        g_ddraw.render.width = g_ddraw.mode.dmPelsWidth;
                        g_ddraw.render.height = g_ddraw.mode.dmPelsHeight;

                        g_ddraw.render.mode.dmPelsWidth = g_ddraw.render.width;
                        g_ddraw.render.mode.dmPelsHeight = g_ddraw.render.height;

                        if (ChangeDisplaySettings(&g_ddraw.render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
                        {
                            /* everything failed, use borderless mode instead */
                            ChangeDisplaySettings(NULL, 0);

                            g_config.windowed = TRUE;
                            g_config.fullscreen = TRUE;
                            border = FALSE;

                            /* prevent OpenGL from going automatically into fullscreen exclusive mode */
                            if (g_ddraw.renderer == ogl_render_main)
                                nonexclusive = TRUE;
                            
                        }
                    }
                }
            }
        }
    }
    
    /* Support downscaling in borderless mode */
    if (g_config.windowed && g_config.fullscreen)
    {
        if (g_ddraw.width > g_ddraw.mode.dmPelsWidth ||
            g_ddraw.height > g_ddraw.mode.dmPelsHeight)
        {
            /* Downscaling requires adjmouse to be enabled */
            g_config.adjmouse = TRUE;

            g_ddraw.render.width = g_ddraw.mode.dmPelsWidth;
            g_ddraw.render.height = g_ddraw.mode.dmPelsHeight;

            g_ddraw.render.mode.dmPelsWidth = g_ddraw.render.width;
            g_ddraw.render.mode.dmPelsHeight = g_ddraw.render.height;
        }
    }

    dd_set_picture(g_ddraw.render.width, g_ddraw.render.height);

    if (nonexclusive || (g_config.nonexclusive && !g_config.windowed && g_ddraw.renderer == ogl_render_main))
    {
        g_ddraw.render.height++;
        g_ddraw.render.opengl_y_align = 1;
    }
    else
    {
        g_ddraw.render.opengl_y_align = 0;
    }

    //dbg_dump_wnd_styles(real_GetWindowLongA(g_ddraw.hwnd, GWL_STYLE), real_GetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE));
    if (g_config.windowed)
    {
        if (g_config.remove_menu && GetMenu(g_ddraw.hwnd))
            SetMenu(g_ddraw.hwnd, NULL);

        if (!g_config.fix_not_responding &&
            g_ddraw.last_msg_pull_tick &&
            g_ddraw.last_msg_pull_tick + 1000 < timeGetTime() &&
            GetCurrentThreadId() == g_ddraw.gui_thread_id && 
            !IsWine())
        {
            /* workaround for "Not Responding" window problem in cnc games */
            g_ddraw.last_msg_pull_tick = timeGetTime();
            MSG msg;
            real_PeekMessageA(&msg, g_ddraw.hwnd, 0, 0, PM_NOREMOVE | PM_QS_INPUT);
        }

        if (!border)
        {
            real_SetWindowLongA(
                g_ddraw.hwnd,
                GWL_STYLE,
                real_GetWindowLongA(
                    g_ddraw.hwnd, 
                    GWL_STYLE) & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU));  
        }
        else
        {
            real_SetWindowLongA(
                g_ddraw.hwnd,
                GWL_STYLE,
                (real_GetWindowLongA(g_ddraw.hwnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW) & ~(WS_MAXIMIZE));

            DWORD class_sytle = GetClassLongA(g_ddraw.hwnd, GCL_STYLE);
            if (class_sytle & CS_NOCLOSE)
            {
                SetClassLongA(g_ddraw.hwnd, GCL_STYLE, class_sytle & ~CS_NOCLOSE);
            }
        }

        LONG exstyle = real_GetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE);

        if ((exstyle & WS_EX_TOOLWINDOW))
        {
            real_SetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE, exstyle & ~(WS_EX_TOOLWINDOW));
        }

        exstyle = real_GetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE);

        if ((exstyle & WS_EX_CLIENTEDGE))
        {
            real_SetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE, exstyle & ~(WS_EX_CLIENTEDGE));
        }

        if (IsWine())
        {
            LONG remove_flags = !g_config.window_resizable ? (WS_MAXIMIZEBOX | WS_THICKFRAME) : 0;

            real_SetWindowLongA(
                g_ddraw.hwnd,
                GWL_STYLE,
                (real_GetWindowLongA(g_ddraw.hwnd, GWL_STYLE) | WS_MINIMIZEBOX) & ~(remove_flags));
        }

        int x = 0;
        int y = 0;
        BOOL clamp = FALSE;

        if (g_config.fullscreen)
        {
            if (GetMenu(g_ddraw.hwnd))
            {
                y = real_GetSystemMetrics(SM_CYMENU);
            }
        }
        else
        {
            POINT origin = dd_window_origin(g_ddraw.render.width, g_ddraw.render.height, &clamp);
            x = origin.x;
            y = origin.y;
        }

        if (util_is_minimized(g_ddraw.hwnd))
            real_ShowWindow(g_ddraw.hwnd, SW_RESTORE);

        RECT dst = { x, y, g_ddraw.render.width + x, g_ddraw.render.height + y };

        LONG style = real_GetWindowLongA(g_ddraw.hwnd, GWL_STYLE);
        exstyle = real_GetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE);

        AdjustWindowRectEx(&dst, style, GetMenu(g_ddraw.hwnd) != NULL, exstyle);

        if (!g_config.fullscreen)
        {
            if (clamp)
            {
                /* The client area moves with its frame */
                RECT before = dst;
                dd_keep_on_screen(&dst);
                x += dst.left - before.left;
                y += dst.top - before.top;
            }

            g_ddraw.window.placed = TRUE;
            g_ddraw.window.centre.x = x + g_ddraw.render.width / 2;
            g_ddraw.window.centre.y = y + g_ddraw.render.height / 2;
            g_ddraw.window.size.cx = g_ddraw.render.width;
            g_ddraw.window.size.cy = g_ddraw.render.height;
        }

        real_SetWindowPos(
            g_ddraw.hwnd, 
            HWND_NOTOPMOST, 
            dst.left, 
            dst.top, 
            (dst.right - dst.left), 
            (dst.bottom - dst.top), 
            SWP_SHOWWINDOW | SWP_FRAMECHANGED);


        dd_renderer_create();

        if (lock_mouse || (g_config.fullscreen && real_GetForegroundWindow() == g_ddraw.hwnd))
            mouse_lock();
    }
    else
    {
        int menu_height = 0;

        if (GetMenu(g_ddraw.hwnd))
        {
            if (1) // g_config.remove_menu || !g_config.nonexclusive)
            {
                SetMenu(g_ddraw.hwnd, NULL);
            }
            else
            {
                menu_height = real_GetSystemMetrics(SM_CYMENU);
            }
        }

        LONG style = real_GetWindowLongA(g_ddraw.hwnd, GWL_STYLE);

        DWORD swp_flags = SWP_SHOWWINDOW;

        if ((style & WS_CAPTION))
        {
            swp_flags |= SWP_FRAMECHANGED;

            real_SetWindowLongA(
                g_ddraw.hwnd,
                GWL_STYLE,
                style & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU));
        }

        LONG exstyle = real_GetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE);

        if ((exstyle & WS_EX_TOOLWINDOW))
        {
            real_SetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE, exstyle & ~(WS_EX_TOOLWINDOW));
        }

        exstyle = real_GetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE);

        if ((exstyle & WS_EX_CLIENTEDGE))
        {
            swp_flags |= SWP_FRAMECHANGED;

            real_SetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE, exstyle & ~(WS_EX_CLIENTEDGE));
        }

        dd_renderer_create();

        {
            if (!zooming && ChangeDisplaySettings(&g_ddraw.render.mode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
            {
                g_ddraw.render.run = FALSE;
                g_config.windowed = TRUE;
                g_config.fullscreen = TRUE;
                return dd_SetDisplayMode(dwWidth, dwHeight, dwBPP, dwFlags);
            }

            /* 
                Fix wayland bug: 
                ChangeDisplaySettings fails silently - enable borderless mode in case display resolution was not changed 
            */
            if (IsWine() && !IsSteamDeck() &&
                (g_ddraw.render.mode.dmPelsWidth != real_GetSystemMetrics(SM_CXSCREEN) || 
                    g_ddraw.render.mode.dmPelsHeight != real_GetSystemMetrics(SM_CYSCREEN)))
            {
                ChangeDisplaySettings(NULL, 0);

                g_ddraw.render.run = FALSE;
                g_config.windowed = TRUE;
                g_config.fullscreen = TRUE;
                return dd_SetDisplayMode(dwWidth, dwHeight, dwBPP, dwFlags);
            }
        }

        if (IsWine())
        {
            real_SetWindowLongA(
                g_ddraw.hwnd, 
                GWL_STYLE, 
                real_GetWindowLongA(g_ddraw.hwnd, GWL_STYLE) | WS_MINIMIZEBOX);
        }

        if (util_is_minimized(g_ddraw.hwnd))
            real_ShowWindow(g_ddraw.hwnd, SW_RESTORE);

        real_SetWindowPos(
            g_ddraw.hwnd,
            HWND_TOPMOST,
            0,
            0,
            g_ddraw.render.width,
            g_ddraw.render.height + menu_height,
            swp_flags);

        g_ddraw.last_set_window_pos_tick = timeGetTime();

        mouse_lock();
    }

    /* Age Of Empires 2 textbox align */
    if (g_ddraw.textbox.hwnd &&
        g_ddraw.textbox.x &&
        g_ddraw.textbox.y &&
        IsWindow(g_ddraw.textbox.hwnd) &&
        GetParent(g_ddraw.textbox.hwnd) == g_ddraw.hwnd)
    {
        char class_name[MAX_PATH] = { 0 };
        GetClassNameA(g_ddraw.textbox.hwnd, class_name, sizeof(class_name) - 1);

        if (_strcmpi(class_name, "Edit") == 0)
        {
            real_SetWindowPos(
                g_ddraw.textbox.hwnd,
                0,
                (int)(g_ddraw.render.viewport.x + (g_ddraw.textbox.x * g_ddraw.render.scale_w)),
                (int)(g_ddraw.render.viewport.y + (g_ddraw.textbox.y * g_ddraw.render.scale_h)),
                0,
                0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER
            );
        }
    }

    RedrawWindow(g_ddraw.hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);

    if (g_ddraw.render.viewport.x != 0 || g_ddraw.render.viewport.y != 0)
    {
        InterlockedExchange(&g_ddraw.render.clear_screen, TRUE);
    }

    if (g_ddraw.render.thread == NULL)
    {
        InterlockedExchange(&g_ddraw.render.palette_updated, TRUE);
        InterlockedExchange(&g_ddraw.render.surface_updated, TRUE);
        ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);

        DWORD tid;
        g_ddraw.render.thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)g_ddraw.renderer, NULL, 0, &tid);
        SetThreadPriority(g_ddraw.render.thread, THREAD_PRIORITY_ABOVE_NORMAL);
    }

    /* Live reload of the settings (dd_reload_settings) */
    SetTimer(g_ddraw.hwnd, IDT_TIMER_RELOAD_SETTINGS, 500, (TIMERPROC)NULL);

    if (dwFlags & SDM_MODE_SET_BY_GAME)
    {
        real_SendMessageA(g_ddraw.hwnd, WM_SIZE_DDRAW, 0, MAKELPARAM(g_ddraw.width, g_ddraw.height));
        real_SendMessageA(g_ddraw.hwnd, WM_DISPLAYCHANGE_DDRAW, g_ddraw.bpp, MAKELPARAM(g_ddraw.width, g_ddraw.height));
    }

    TRACE(
        "     render res=%ux%u (viewport res=%ux%u, x=%d, y=%d)\n", 
        g_ddraw.render.width, 
        g_ddraw.render.height,
        g_ddraw.render.viewport.width,
        g_ddraw.render.viewport.height,
        g_ddraw.render.viewport.x,
        g_ddraw.render.viewport.y);

    TRACE(
        "     windowed=%s, fullscreen=%s, adjmouse=%s\n", 
        g_config.windowed ? "true" : "false",
        g_config.fullscreen ? "true" : "false",
        g_config.adjmouse ? "true" : "false");

    return DD_OK;
}

HRESULT dd_SetCooperativeLevel(HWND hwnd, DWORD dwFlags)
{
    dbg_dump_scl_flags(dwFlags);

    if (!hwnd)
    {
        if (!g_ddraw.hwnd && g_config.fake_mode[0])
        {
            EnumThreadWindows(GetCurrentThreadId(), (WNDENUMPROC)util_enum_thread_wnd_proc, 0);
            hwnd = g_ddraw.hwnd;

            if (!hwnd)
                return DD_OK;
        }
        else
        {
            return DD_OK;
        }
    }

    /* display: the first exclusive fullscreen request decides for good: later
       DDSCL_NORMAL calls are usually the game cleaning up, and an Alt+Enter
       toggle has to survive the game calling this again. A normal (windowed)
       request only counts before that. */
    if (dwFlags & DDSCL_FULLSCREEN)
    {
        if (!g_ddraw.saw_fullscreen)
        {
            g_ddraw.saw_fullscreen = TRUE;

            DISPLAY_KIND kind = ds_display_start(g_config.display, 1);
            g_config.windowed = kind == DISPLAY_KIND_WINDOW;
            g_config.fullscreen = kind == DISPLAY_KIND_DESKTOP;
        }
    }
    else if ((dwFlags & DDSCL_NORMAL) && !g_ddraw.saw_fullscreen && !g_ddraw.saw_normal)
    {
        g_ddraw.saw_normal = TRUE;

        DISPLAY_KIND kind = ds_display_start(g_config.display, 0);
        g_config.windowed = kind == DISPLAY_KIND_WINDOW;
        g_config.fullscreen = kind == DISPLAY_KIND_DESKTOP;
    }

    if (!g_ddraw.hwnd)
    {
        g_ddraw.hwnd = hwnd;
    }

    if (!g_ddraw.wndproc)
    {
        hook_init();

        g_ddraw.wndproc = (WNDPROC)real_SetWindowLongA(g_ddraw.hwnd, GWL_WNDPROC, (LONG)fake_WndProc);
        g_ddraw.gui_thread_id = GetWindowThreadProcessId(g_ddraw.hwnd, NULL);

        keyboard_hook_init();

        if (!g_ddraw.render.hdc)
        {
            g_ddraw.render.hdc = GetDC(g_ddraw.hwnd);
        }

        if (!g_config.devmode)
        {
            InterlockedExchange((LONG*)&g_ddraw.old_cursor, (LONG)real_SetCursor(LoadCursor(NULL, IDC_ARROW)));
        }

        int cursor_count = real_ShowCursor(TRUE) - 1;
        InterlockedExchange((LONG*)&g_ddraw.show_cursor_count, cursor_count);
        real_ShowCursor(FALSE);

        /* Make sure the cursor is visible in windowed mode initially */
        if (g_config.windowed && !g_config.fullscreen && !g_config.devmode && cursor_count < 0)
        {
            while (real_ShowCursor(TRUE) < 0);
        }

        /* Starcraft locks the cursor before ddraw.dll was loaded */
        if (g_config.windowed)
        {
            real_ClipCursor(NULL);
        }

        GetWindowText(g_ddraw.hwnd, (LPTSTR)&g_ddraw.title, sizeof(g_ddraw.title));

        g_ddraw.isredalert = strcmp(g_ddraw.title, "Red Alert") == 0;
        g_ddraw.iscnc1 = strcmp(g_ddraw.title, "Command & Conquer") == 0;
        g_ddraw.iskkndx = strcmp(g_ddraw.title, "KKND Xtreme") == 0;
        g_ddraw.isworms2 = strcmp(g_ddraw.title, "worms2") == 0;

        /* Worms 2: only the known executable */
        if (g_ddraw.isworms2 &&
            memcmp((char*)GetModuleHandleA(NULL) + 0x00010000, "\x17\x81\xC2\x00\x80\x00\x00\x89", 8) != 0)
        {
            g_ddraw.isworms2 = FALSE;
        }
    }

    if ((dwFlags & DDSCL_NORMAL) && !(dwFlags & DDSCL_FULLSCREEN))
    {
        if (g_config.fake_mode[0])
        {
            char* e = &g_config.fake_mode[0];

            unsigned long width = strtoul(e, &e, 0);
            unsigned long height = strtoul(e + 1, &e, 0);
            unsigned long bpp = strtoul(e + 1, &e, 0);

            dd_SetDisplayMode(width, height, bpp, 0);
        }
        else if (!g_ddraw.width)
        {
            RECT rc = { 0 };
            real_GetClientRect(hwnd, &rc);

            BOOL popup = real_GetWindowLongA(hwnd, GWL_STYLE) & WS_POPUP;

            if ((rc.right < real_GetSystemMetrics(SM_CXSCREEN) && rc.bottom < real_GetSystemMetrics(SM_CYSCREEN)) || !popup)
            {
                TRACE("     client rect=%dx%d\n", rc.right, rc.bottom);

                g_ddraw.windowed_hack = TRUE;

                /* A windowed program draws to the desktop, so by default it
                   gets the desktop's bit depth, as with Windows' own
                   DirectDraw; Blt converts from other depths */
                DWORD bpp = 16;
                if (g_config.windowed_bpp == 16 || g_config.windowed_bpp == 32)
                {
                    bpp = (DWORD)g_config.windowed_bpp;
                }
                else
                {
                    HDC dc = GetDC(NULL);
                    int desktop_bpp = dc ? GetDeviceCaps(dc, BITSPIXEL) : 0;
                    if (dc)
                        ReleaseDC(NULL, dc);

                    if (desktop_bpp == 32 || desktop_bpp == 24)
                        bpp = 32;
                }

                TRACE("     windowed bpp=%u\n", bpp);

                /* The program chose its window's size, and that size is its
                   mode. The window gets window_size, centred where the
                   program put it (window_position=auto) */
                POINT origin = { 0, 0 };
                real_ClientToScreen(hwnd, &origin);

                if (!g_ddraw.window.placed)
                {
                    g_ddraw.window.placed = TRUE;
                    g_ddraw.window.centre.x = origin.x + rc.right / 2;
                    g_ddraw.window.centre.y = origin.y + rc.bottom / 2;
                    g_ddraw.window.size.cx = rc.right;
                    g_ddraw.window.size.cy = rc.bottom;
                }

                dd_SetDisplayMode(rc.right, rc.bottom, bpp, 0);
            }
        }
    }
    else
    {
        g_ddraw.windowed_hack = FALSE;
    }

    return DD_OK;
}

HRESULT dd_WaitForVerticalBlank(DWORD dwFlags, HANDLE hEvent)
{
    if (g_config.maxgameticks == -2)
    {
        if (fpsl_wait_for_vblank())
            return DD_OK;
    }

    if (g_config.maxgameticks < 0)
        return DD_OK;

    vbeam_wait((dwFlags & DDWAITVB_BLOCKEND) != 0);

    return DD_OK;
}

ULONG dd_AddRef()
{
    return InterlockedIncrement(&g_ddraw.ref);
}

ULONG dd_Release()
{
    LONG ref = InterlockedDecrement(&g_ddraw.ref);

    if (ref == 0)
    {
        /* Let the cursor go (ClipCursor) and show it again. The state below is
           wiped, so nothing would unlock it later if the game keeps running */
        mouse_unlock();

        if (g_ddraw.hwnd)
            KillTimer(g_ddraw.hwnd, IDT_TIMER_RELOAD_SETTINGS);

        if (g_ddraw.bpp)
        {
            cfg_save();
        }

        if (g_ddraw.render.run)
        {
            EnterCriticalSection(&g_ddraw.cs);
            g_ddraw.render.run = FALSE;
            ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
            LeaveCriticalSection(&g_ddraw.cs);

            if (g_ddraw.render.thread)
            {
                WaitForSingleObject(g_ddraw.render.thread, INFINITE);
                g_ddraw.render.thread = NULL;
            }
        }

        if (!g_config.windowed)
        {
            ChangeDisplaySettings(NULL, 0);
        }

        if (g_ddraw.renderer == ogl_render_main)
        {
            ogl_release();
        }
        else if (g_ddraw.renderer == d3d11_render_main)
        {
            d3d11_release();
        }

        if (g_ddraw.render.hdc)
        {
            ReleaseDC(g_ddraw.hwnd, g_ddraw.render.hdc);
            g_ddraw.render.hdc = NULL;
        }

        if (g_ddraw.ticks_limiter.htimer)
        {
            CancelWaitableTimer(g_ddraw.ticks_limiter.htimer);
            CloseHandle(g_ddraw.ticks_limiter.htimer);
            g_ddraw.ticks_limiter.htimer = NULL;
        }

        if (g_fpsl.htimer)
        {
            CancelWaitableTimer(g_fpsl.htimer);
            CloseHandle(g_fpsl.htimer);
            g_fpsl.htimer = NULL;
        }

        if (g_ddraw.real_dd)
        {
            g_ddraw.real_dd->lpVtbl->Release(g_ddraw.real_dd);
        }

        DeleteCriticalSection(&g_ddraw.cs);

        if (g_ddraw.hwnd && IsWindow(g_ddraw.hwnd))
        {
            /* restore old wndproc, subsequent ddraw creation will otherwise fail */
            real_SetWindowLongA(g_ddraw.hwnd, GWL_WNDPROC, (LONG)g_ddraw.wndproc);
        }
        
        memset(&g_ddraw, 0, sizeof(g_ddraw));
        
        return 0;
    }

    if (ref < 0)
    {
        InterlockedExchange(&g_ddraw.ref, 0);
        return 0;
    }

    return (ULONG)ref;
}

HRESULT dd_GetAvailableVidMem(LPDDSCAPS lpDDCaps, LPDWORD lpdwTotal, LPDWORD lpdwFree)
{
    if (lpdwTotal)
        *lpdwTotal = 16777216;

    if (lpdwFree)
        *lpdwFree = 16777216;

    return DD_OK;
}

HRESULT dd_TestCooperativeLevel()
{
    if (g_config.limiter_type == LIMIT_TESTCOOP && g_ddraw.ticks_limiter.tick_length > 0)
    {
        g_ddraw.ticks_limiter.dds_unlock_limiter_disabled = TRUE;
        util_limit_game_ticks();
    }

    return DD_OK;
}

HRESULT dd_GetDeviceIdentifier(LPDDDEVICEIDENTIFIER pDDDI, DWORD dwFlags, REFIID riid)
{
    if (!pDDDI)
        return DDERR_INVALIDPARAMS;

    if (IsEqualGUID(&IID_IDirectDraw7, riid))
    {
        memset(pDDDI, 0, sizeof(DDDEVICEIDENTIFIER2));
    }
    else
    {
        memset(pDDDI, 0, sizeof(DDDEVICEIDENTIFIER));
    }
    
    return DD_OK;
}

/* ------------------------------------------------------------- renderers */

/* Direct3D 11 then OpenGL, or OpenGL first with renderer=opengl. One that
 * fails isn't tried again in this run; when both have, the black window */
static BOOL g_dd_d3d11_failed;
static BOOL g_dd_ogl_failed;

static DD_RENDERER dd_next_renderer(void)
{
    /* Not on a window Direct3D 11 has shown its frames in: OpenGL's wouldn't */
    if (_strcmpi(g_config.renderer, "opengl") == 0 && !g_dd_ogl_failed &&
        (g_dd_d3d11_failed || !d3d11_flipped(g_ddraw.hwnd)))
        return ogl_render_main;

    if (!g_dd_d3d11_failed)
        return d3d11_render_main;

    if (!g_dd_ogl_failed)
        return ogl_render_main;

    return blank_render_main;
}

static const char* dd_renderer_name(DD_RENDERER renderer)
{
    return
        renderer == d3d11_render_main ? "Direct3D 11" :
        renderer == ogl_render_main ? "OpenGL" : "none";
}

static void dd_renderer_mark_failed(void)
{
    if (g_ddraw.renderer == d3d11_render_main)
        g_dd_d3d11_failed = TRUE;
    else if (g_ddraw.renderer == ogl_render_main)
        g_dd_ogl_failed = TRUE;

    DD_RENDERER next = dd_next_renderer();

    TRACE("     %s failed, %s next\n", dd_renderer_name(g_ddraw.renderer), dd_renderer_name(next));

    g_ddraw.renderer = next;
}

/* Gets the renderer ready for the window; one that can't be is swapped for
 * the next */
void dd_renderer_create(void)
{
    for (;;)
    {
        if (g_ddraw.renderer == d3d11_render_main)
        {
            if (d3d11_create())
                return;

            d3d11_release();
        }
        else if (g_ddraw.renderer == ogl_render_main)
        {
            if (ogl_create())
                return;

            ogl_release();
        }
        else
        {
            return;
        }

        dd_renderer_mark_failed();
    }
}

DWORD dd_renderer_failed(void)
{
    dd_renderer_mark_failed();
    dd_renderer_create();

    return g_ddraw.renderer();
}

/* -------------------------------------------------------------- stepping aside */

/* 0: not decided yet, 1: DirectCRT, 2: Windows' ddraw.dll */
static LONG g_dd_aside;
static HMODULE g_dd_system_dll;

BOOL dd_stepped_aside(void)
{
    return g_dd_aside == 2;
}

FARPROC dd_system_proc(const char* name)
{
    if (!g_dd_system_dll)
    {
        char path[MAX_PATH];
        UINT len = GetSystemDirectoryA(path, sizeof(path));

        if (len && len < sizeof(path) - sizeof("\\ddraw.dll"))
        {
            strcat(path, "\\ddraw.dll");
            g_dd_system_dll = real_LoadLibraryA(path);

            if (g_dd_system_dll == g_ddraw_module)
                g_dd_system_dll = NULL;
        }
    }

    return g_dd_system_dll ? real_GetProcAddress(g_dd_system_dll, name) : NULL;
}

BOOL dd_step_aside(void)
{
    if (g_dd_aside)
        return g_dd_aside == 2;

    DD_RENDERER renderer;

    while ((renderer = dd_next_renderer()) != blank_render_main)
    {
        if (renderer == d3d11_render_main ? d3d11_probe() : ogl_probe())
        {
            g_dd_aside = 1;
            return FALSE;
        }

        g_ddraw.renderer = renderer;
        dd_renderer_mark_failed();
    }

    g_ddraw.renderer = NULL;
    g_dd_aside = 2;

    TRACE("     Neither Direct3D 11 nor OpenGL works: Windows' ddraw.dll instead\n");

    keyboard_hook_exit();
    dinput_hook_exit();
    hook_exit();

    MessageBoxA(
        NULL,
        "DirectCRT could not start Direct3D 11 or OpenGL on this computer, so this game "
        "runs with Windows' own DirectDraw, without DirectCRT.\n\n"
        "Updating the graphics driver may help.",
        "DirectCRT",
        MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);

    return TRUE;
}

static HRESULT dd_system_create(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter)
{
    typedef HRESULT(WINAPI* DIRECTDRAWCREATEEXPROC)(GUID*, LPVOID*, REFIID, IUnknown*);

    if (lplpDD)
        *lplpDD = NULL;

    if (IsEqualGUID(&IID_IDirectDraw7, iid))
    {
        DIRECTDRAWCREATEEXPROC create_ex = (DIRECTDRAWCREATEEXPROC)dd_system_proc("DirectDrawCreateEx");

        return create_ex ? create_ex(lpGuid, lplpDD, iid, pUnkOuter) : DDERR_NODIRECTDRAWSUPPORT;
    }

    DIRECTDRAWCREATEPROC create = (DIRECTDRAWCREATEPROC)dd_system_proc("DirectDrawCreate");
    LPDIRECTDRAW dd = NULL;

    HRESULT hr = create ? create(lpGuid, &dd, pUnkOuter) : DDERR_NODIRECTDRAWSUPPORT;

    if (SUCCEEDED(hr) && !IsEqualGUID(&IID_IDirectDraw, iid))
    {
        hr = IDirectDraw_QueryInterface(dd, iid, lplpDD);
        IDirectDraw_Release(dd);
    }
    else if (SUCCEEDED(hr))
    {
        *lplpDD = dd;
    }

    return hr;
}

HRESULT dd_CreateEx(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter)
{
    if (dd_step_aside())
        return dd_system_create(lpGuid, lplpDD, iid, pUnkOuter);

    if (InterlockedExchangeAdd((LONG*)&g_ddraw.ref, 0) == 0)
    {
        InterlockedIncrement(&g_ddraw.ref);

        InitializeCriticalSection(&g_ddraw.cs);

        g_ddraw.render.sem = CreateSemaphore(NULL, 0, 1, NULL);
        g_blt_use_avx = util_is_avx_supported();

        if (g_config.minfps > 1000)
            g_config.minfps = 1000;

        if (g_config.minfps > 0)
            g_ddraw.minfps_tick_len = (DWORD)(1000.0f / g_config.minfps);


        HANDLE (WINAPI *createTimerExW)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD) = NULL;

        if (!IsWine() && IsWindows10Version1803OrGreater())
        {
            createTimerExW = (void*)real_GetProcAddress(real_LoadLibraryA("Kernel32.dll"), "CreateWaitableTimerExW");
        }

        DWORD timer_flags = CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION;

        /* can't fully set it up here due to missing g_ddraw.mode.dmDisplayFrequency  */
        g_fpsl.htimer = createTimerExW ? createTimerExW(NULL, NULL, timer_flags, TIMER_ALL_ACCESS) : NULL;
        
        if (!g_fpsl.htimer) 
            g_fpsl.htimer = CreateWaitableTimer(NULL, TRUE, NULL);

        if (g_config.maxgameticks > 0 && g_config.maxgameticks <= 1000)
        {
            g_ddraw.ticks_limiter.htimer = createTimerExW ? createTimerExW(NULL, NULL, timer_flags, TIMER_ALL_ACCESS) : NULL;

            if (!g_ddraw.ticks_limiter.htimer)
                g_ddraw.ticks_limiter.htimer = CreateWaitableTimer(NULL, TRUE, NULL);

            float len = 1000.0f / g_config.maxgameticks;
            g_ddraw.ticks_limiter.tick_length_ns = (LONGLONG)(len * 10000);
            g_ddraw.ticks_limiter.tick_length = (DWORD)(len + 0.5f);
        }


        DWORD system_affinity;
        DWORD proc_affinity;
        HANDLE proc = GetCurrentProcess();

        if (g_config.singlecpu)
        {
            if (!IsWine() && IsWindows11Version24H2OrGreater())
            {
                if (GetProcessAffinityMask(proc, &proc_affinity, &system_affinity))
                    SetProcessAffinityMask(proc, system_affinity);

                util_set_process_affinity();
            }
            else
            {
                SetProcessAffinityMask(proc, 1);
            }
        }
        else if (GetProcessAffinityMask(proc, &proc_affinity, &system_affinity))
        {
            SetProcessAffinityMask(proc, system_affinity);
        }

        if (GetProcessAffinityMask(proc, &proc_affinity, &system_affinity))
        {
            TRACE("     proc_affinity=%08X, system_affinity=%08X\n", proc_affinity, system_affinity);
        }

        
        /* The one dd_step_aside found working */
        g_ddraw.renderer = dd_next_renderer();

        LONG ref = InterlockedDecrement(&g_ddraw.ref);

        if (ref < 0)
            InterlockedExchange(&g_ddraw.ref, 0);
    }

    IDirectDrawImpl* dd = (IDirectDrawImpl*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IDirectDrawImpl));
    memcpy(&dd->guid, iid, sizeof(dd->guid));

    if (IsEqualGUID(&IID_IDirectDraw, iid))
    {
        TRACE("     GUID = %08X (IID_IDirectDraw), ddraw = %p\n", ((GUID*)iid)->Data1, dd);

        dd->lpVtbl = &g_dd_vtbl1;
    }
    else
    {
        TRACE("     GUID = %08X (IID_IDirectDrawX), ddraw = %p\n", ((GUID*)iid)->Data1, dd);

        dd->lpVtbl = &g_dd_vtblx;
    }

    IDirectDraw_AddRef(dd);

    *lplpDD = (LPVOID)dd;

    return DD_OK;
}
