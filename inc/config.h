#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include "ini.h"
#include "crt_auto.h"
#include "display_settings.h"

#define FILE_EXISTS(a) (GetFileAttributes(a) != INVALID_FILE_ATTRIBUTES)

typedef struct CNCDDRAWCONFIG
{
    RECT window_rect; /* the window's client area: left/top -32000 = from window_position, right/bottom 0 = from window_size */
    char ini_path[MAX_PATH];
    char game_path[MAX_PATH];
    char dll_path[MAX_PATH];
    char process_file_name[MAX_PATH];
    char dll_file_name[MAX_PATH];
    char process_file_ext[MAX_PATH];
    char dll_file_ext[MAX_PATH];
    char game_section[MAX_PATH];
    INIFILE ini;

    /* Optional settings */

    DISPLAY_MODE display;
    WINDOW_SIZE window_size;
    WINDOW_POSITION window_position;
    BOOL window_decorations;
    BOOL window_resizable;
    REMEMBER_WINDOW remember_window;
    int maxfps;
    BOOL vsync;
    BOOL adjmouse;
    CRT_AUTO_SETTINGS crt; /* shader and DOSBox Staging's [render] settings: aspect, viewport, integer_scaling, colour */
    char renderer[256];
    BOOL devmode;
    int anti_aliased_fonts_min_size;
    int min_font_size;
    char screenshot_dir[MAX_PATH];

    /* Set at runtime from 'display' and Alt+Enter: windowed=1 fullscreen=0 is
       a window, 0/1 fullscreen at the desktop resolution, 0/0 a mode switch
       to the game's resolution, and 1/1 borderless, the mode switch's
       fallback */
    BOOL fullscreen;
    BOOL windowed;

    /* Compatibility settings */

    BOOL noactivateapp;
    int maxgameticks;
    int limiter_type;
    int minfps;
    BOOL nonexclusive;
    BOOL singlecpu;
    int resolutions;
    int fixchilds;
    BOOL hook_peekmessage;
    int windowed_bpp; /* 0 = the desktop's */

    /* Undocumented settings */

    BOOL fix_alt_key_stuck;
    BOOL fix_not_responding;
    BOOL no_compat_warning;
    int guard_lines;
    int max_resolutions;
    BOOL lock_surfaces;
    BOOL flipclear;
    BOOL rgb555;
    BOOL no_dinput_hook;
    BOOL center_cursor_fix;
    char fake_mode[128];
    BOOL lock_mouse_top_left;
    char win_version[32];
    int hook;
    BOOL limit_gdi_handles;
    BOOL remove_menu;
    int refresh_rate;
    int terminate_process;

    /* Hotkeys */

    struct
    {
        int toggle_fullscreen;
        int toggle_fullscreen2;
        int toggle_maximize;
        int toggle_maximize2;
        int unlock_cursor1;
        int unlock_cursor2;
        BOOL unlock_cursor_middleclick;
        int screenshot;
    } hotkeys;

    /* Game specific settings */

    BOOL armadahack;
    BOOL tshack;
    BOOL infantryhack;
    BOOL stronghold_hack;
    BOOL mgs_hack;
    BOOL tlc_hack;
    BOOL carma95_hack;
    BOOL sirtech_hack;
    BOOL flightsim98_hack;
    BOOL darkcolony_hack;

} CNCDDRAWCONFIG;

extern CNCDDRAWCONFIG g_config;

void cfg_load();
void cfg_save();
BOOL cfg_poll_crt_auto_settings(CRT_AUTO_SETTINGS* settings);

#endif
