#ifndef DD_H 
#define DD_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ddraw.h"
#include "crt_auto.h"

typedef HRESULT(WINAPI* DIRECTDRAWCREATEPROC)(GUID FAR*, LPDIRECTDRAW FAR*, IUnknown FAR*);

ULONG dd_AddRef();
ULONG dd_Release();
HRESULT dd_EnumDisplayModes(DWORD dwFlags, LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK lpEnumModesCallback);
HRESULT dd_GetCaps(LPDDCAPS_DX1 lpDDDriverCaps, LPDDCAPS_DX1 lpDDEmulCaps);
HRESULT dd_GetDisplayMode(LPDDSURFACEDESC lpDDSurfaceDesc);
HRESULT dd_GetMonitorFrequency(LPDWORD lpdwFreq);
HRESULT dd_GetVerticalBlankStatus(LPBOOL lpbIsInVB);
HRESULT dd_GetScanLine(LPDWORD lpdwScanLine);
void dd_set_refresh(DWORD game_hz); /* the emulated display's rate, after a mode set; 0 = the game didn't ask */
HRESULT dd_RestoreDisplayMode();
HRESULT dd_SetCooperativeLevel(HWND hwnd, DWORD dwFlags);
HRESULT dd_SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwFlags);
void dd_reload_settings(BOOL now); /* live reload; now = without waiting for the file to settle */
HRESULT dd_WaitForVerticalBlank(DWORD dwFlags, HANDLE hEvent);
HRESULT dd_GetAvailableVidMem(LPDDSCAPS lpDDCaps, LPDWORD lpdwTotal, LPDWORD lpdwFree);
HRESULT dd_TestCooperativeLevel();
HRESULT dd_GetDeviceIdentifier(LPDDDEVICEIDENTIFIER pDDDI, DWORD dwFlags, REFIID riid);
HRESULT dd_CreateEx(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter);

/* The renderer: dd_renderer_create gets it ready for the window (game
 * thread); dd_renderer_failed, from a render thread that couldn't start,
 * switches to the next and runs it */
void dd_renderer_create(void);
DWORD dd_renderer_failed(void);

/* Neither Direct3D 11 nor OpenGL works: DirectCRT steps aside, its hooks
 * undone, and DirectDraw is Windows' own (decided at the first create) */
BOOL dd_step_aside(void);
BOOL dd_stepped_aside(void);
FARPROC dd_system_proc(const char* name);

#define FIX_CHILDS_DISABLED 0
#define FIX_CHILDS_DETECT 1
#define FIX_CHILDS_DETECT_PAINT 2
#define FIX_CHILDS_DETECT_HIDE 3
#define FIX_CHILDS_DETECT_HIDE_NOSCALE 4

#define RESLIST_NORMAL 0
#define RESLIST_MINI 1
#define RESLIST_FULL 2

#define SDM_MODE_SET_BY_GAME 0x00000001l
#define SDM_LEAVE_WINDOWED   0x00000002l
#define SDM_LEAVE_FULLSCREEN 0x00000004l

#define LIMIT_AUTO 0
#define LIMIT_TESTCOOP 1
#define LIMIT_BLTFAST 2
#define LIMIT_UNLOCK 3
#define LIMIT_PEEKMESSAGE 4

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#ifndef CREATE_WAITABLE_TIMER_MANUAL_RESET
#define CREATE_WAITABLE_TIMER_MANUAL_RESET 0x00000001
#endif

#ifndef GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif

#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#endif

#if (_WIN32_WINNT < _WIN32_WINNT_WIN2K)
#define GdiTransparentBlt TransparentBlt 
#endif

typedef struct SPEEDLIMITER
{
    DWORD tick_length;
    LONGLONG tick_length_ns;
    HANDLE htimer;
    LARGE_INTEGER due_time;
    BOOL dds_unlock_limiter_disabled;
} SPEEDLIMITER;

struct IDirectDrawSurfaceImpl;

extern struct CNCDDRAW g_ddraw;

typedef struct CNCDDRAW
{
    LONG ref;

    DWORD width;
    DWORD height;
    DWORD bpp;
    DWORD refresh_asked; /* the rate the game asked for in SetDisplayMode(X); 0 = none */
    
    DEVMODE mode;
    struct IDirectDrawSurfaceImpl* primary;
    char title[128];
    
    CRITICAL_SECTION cs;

    /* real export from system32\ddraw.dll */
    HMODULE real_dll;
    DIRECTDRAWCREATEPROC DirectDrawCreate;
    LPDIRECTDRAW real_dd;

    struct
    {
        int width;
        int height;
        int opengl_y_align;

        HDC hdc;
        int* tex;

        HANDLE thread;
        BOOL run;
        HANDLE sem;
        DEVMODE mode;
        struct { int width; int height; int x; int y; } viewport;
        CRT_AUTO_LAYOUT crt; /* with shader=crt-auto: the shader and picture size it chose */

        LONG palette_updated;
        LONG surface_updated;
        LONG picture_changed; /* crt and viewport are new: the renderer sets its passes up again */
        LONG clear_screen;
        LONG screen_updated;

        float scale_w;
        float scale_h;
        float unscale_w;
        float unscale_h;
    } render;

    struct
    {
        float scale_x;
        float scale_y;
        float unscale_x;
        float unscale_y;

        int y_adjust;
        int x_adjust;
        RECT rc;
    } mouse;

    DWORD(WINAPI* renderer)(void);
    HWND hwnd;
    WNDPROC wndproc;
    struct { DWORD x; DWORD y; } cursor;
    BOOL isredalert;
    BOOL iscnc1;
    BOOL iskkndx;
    BOOL isworms2;
    HCURSOR old_cursor;
    int show_cursor_count;
    BOOL alt_key_down;
    BOOL bnet_active;
    BOOL bnet_was_fullscreen;
    BOOL bnet_was_upscaled;
    BOOL bnet_was_resizable;
    RECT bnet_win_rect;
    POINT bnet_pos;
    void* last_freed_palette; /* Dungeon Keeper hack */
    void* last_freed_surface; /* Nox hack */
    BOOL child_window_exists;
    BOOL got_child_windows;
    DWORD last_set_window_pos_tick; /* WINE hack */
    DWORD last_msg_pull_tick;
    SPEEDLIMITER ticks_limiter;
    DWORD minfps_tick_len;
    DWORD gui_thread_id;
    BOOL windowed_hack;
    BOOL saw_fullscreen; /* display: what the game has asked for so far (dd_SetCooperativeLevel) */
    BOOL saw_normal;

    struct
    {
        BOOL placed;        /* centre and size are set: where the window was last */
        POINT centre;       /* of the client area, on the desktop */
        SIZE size;          /* the client area */
    } window;               /* window_position=auto and window_size */

    struct
    {
        HWND hwnd;
        int x;
        int y;
    } textbox; /* Age Of Empires 2 textbox align */

    struct
    {
        BOOL enabled;
    } zoom;
} CNCDDRAW;

#endif
