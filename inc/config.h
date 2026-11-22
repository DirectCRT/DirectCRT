#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include "ini.h"

#define FILE_EXISTS(a) (GetFileAttributes(a) != INVALID_FILE_ATTRIBUTES)

typedef struct CNCDDRAWCONFIG
{
    RECT window_rect;
    char ini_path[MAX_PATH];
    char game_path[MAX_PATH];
    char dll_path[MAX_PATH];
    char process_file_name[MAX_PATH];
    char dll_file_name[MAX_PATH];
    char process_file_ext[MAX_PATH];
    char dll_file_ext[MAX_PATH];
    char game_section[MAX_PATH];
    INIFILE ini;
    BOOL d3d9on12;
    BOOL opengl_core;

    /* Optional settings */

    BOOL fullscreen;
    BOOL windowed;
    BOOL windowed_auto; /* windowed=auto: follow what the game asks for */
    BOOL fullscreen_setting; /* fullscreen as configured; with windowed=auto it only applies to fullscreen games */
    BOOL maintas;
    char aspect_ratio[16];
    BOOL boxing;
    int maxfps;
    BOOL vsync;
    BOOL adjmouse;
    char shader[MAX_PATH];
    BOOL crt_auto; /* shader=crt-auto: DOSBox Staging's crt-auto for VGA, with its own scaling */
    char renderer[256];
    BOOL devmode;
    BOOL border;
    int save_settings;
    BOOL resizable;
    int d3d9_filter;
    int anti_aliased_fonts_min_size;
    int min_font_size;
    int center_window;
    char inject_resolution[128];
    BOOL vhack;
    char screenshot_dir[MAX_PATH];
    BOOL toggle_borderless;
    BOOL toggle_upscaled;

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
    BOOL window_is_programs; /* set at runtime: a windowed program's own window, not saved */

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

#endif
