#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "overlay_icons.h"
#include "vbeam.h"

/* The ini's lines and the OSD's tabs. Keep the ini's wording in the help: the
   OSD shows it as it is */

#define STR_(x) #x
#define STR(x) STR_(x)

#define INI_TEXT(t) { SETTING_TEXT, NULL, NULL, t }
#define OSD_HEADING(tab, label, help) { SETTING_HEADING, NULL, NULL, help, tab, label }

#define T_DISPLAY "Display"
#define T_PICTURE "Picture"
#define T_COLOUR "Colour"
#define T_TIMING "Timing"
#define T_OVERLAY "Overlay"
#define T_OTHER "Other"
#define T_COMPAT "Compatibility"
#define T_KEYS "Hotkeys"

static const char* const g_tabs[] = {
    T_DISPLAY, T_PICTURE, T_COLOUR, T_TIMING, T_OVERLAY, T_OTHER, T_COMPAT, T_KEYS, NULL
};

#define BOOL_TF "false|true"
#define BOOL_ONOFF "off|on"

static const SETTING g_settings[] = {
    INI_TEXT(
        "; DirectCRT - https://github.com/vvekic/DirectCRT\n"
        "\n"
        "[directcrt]\n"
        "; ### Optional settings ###\n"
        "; Use the following settings to adjust the look and feel to your liking\n"
        "; Where DOSBox Staging has the same setting, it has DOSBox's name and values\n"
        "; Changes apply while the game runs, when this file is saved, except for the compatibility settings,\n"
        "; which need a restart.\n"
        "; The settings OSD (Alt+F12) changes them while the game runs\n"
        "\n"
        "\n"),

    { SETTING_CHOICE, "display", "auto",
        "Fullscreen or a window\n"
        "auto = what the game asks for: fullscreen for fullscreen games, a window for games that run in their own window\n"
        "window = always a window\n"
        "fullscreen = always fullscreen\n"
        "original = fullscreen games switch the monitor to the game's resolution, or the closest mode the monitor has,\n"
        "  as with Windows' own DirectDraw; for real CRTs and external scalers. With one screen pixel per line there's\n"
        "  no CRT shading, but the colour adjustments still apply. Games in their own window get a window\n"
        "Fullscreen is at the desktop resolution, except with 'original'. Alt+Enter switches between a window and fullscreen",
        T_DISPLAY, "Display", "auto|window|fullscreen|original", 0, 0, SETTING_LIVE, .icon = ICON_FULLSCREEN },

    { SETTING_BOOL, "fullscreen_minimize", "off",
        "Minimize fullscreen when another window gets the focus (Alt+Tab, the Windows key, a click on another screen)\n"
        "off = fullscreen stays shown behind the other windows, as a borderless window; the taskbar and the other\n"
        "  screens work as usual\n"
        "on = it's minimized, as exclusive fullscreen did; for games that misbehave when they keep being shown\n"
        "Note: display=original always minimizes, the screen going back to the desktop's resolution",
        T_DISPLAY, "Minimize when not in focus", BOOL_ONOFF, 0, 0, SETTING_LIVE, .icon = ICON_FULLSCREEN_EXIT },

    { SETTING_STRING, "window_size", "default",
        "The size of the window's contents, without its title bar and borders\n"
        "default = medium\n"
        "small, medium, large = 4:3 sizes of 50%, 74% and 90% of the desktop height\n"
        "unscaled = the game's own resolution at 1:1, for games that misbehave when scaled\n"
        "WxH = a size in logical units (e.g. 1280x960; with display scaling at 150%, 1920x1440 pixels)\n"
        "WxHpx = a size in pixels (e.g. 1920x1440px)\n"
        "Note: The settings for the picture (aspect, integer_scaling, viewport) place it in the window, as in fullscreen",
        T_DISPLAY, "Window size", "default|small|medium|large|unscaled", 0, 0, SETTING_LIVE, .icon = ICON_PHOTO_SIZE_SELECT_LARGE },

    { SETTING_STRING, "window_position", "auto",
        "Where the window goes\n"
        "auto = centred on the screen, or where the game put its own window\n"
        "X,Y = the top left corner in logical units\n"
        "X,Ypx = in pixels\n"
        "X,Y% = a percentage of the desktop (e.g. 10,5%)",
        T_DISPLAY, "Window position", "auto", 0, 0, SETTING_LIVE, .icon = ICON_OPEN_WITH },

    { SETTING_BOOL, "window_decorations", "on",
        "Show the window's title bar and borders",
        T_DISPLAY, "Title bar and borders", BOOL_ONOFF, 0, 0, SETTING_LIVE, .icon = ICON_WEB_ASSET },

    { SETTING_BOOL, "window_resizable", "on",
        "Let the user resize and maximize the window",
        T_DISPLAY, "Resizable window", BOOL_ONOFF, 0, 0, SETTING_LIVE, .icon = ICON_OPEN_IN_FULL },

    { SETTING_BOOL, "remember_window", "off",
        "Save the window's size and position here when the game exits, for the next start\n"
        "Note: Alt+Enter's state isn't saved; every start uses 'display'",
        T_DISPLAY, "Remember the window", BOOL_ONOFF, 0, 0, SETTING_LIVE, .icon = ICON_SAVE },

    { SETTING_INT, "maxfps", "-1",
        "Real rendering rate\n"
        "-1 = the screen's refresh rate\n"
        "0 = unlimited\n"
        "1-1000 = at most this many frames a second\n"
        "Note: Does not have an impact on the game speed, to limit your game speed use 'maxgameticks='",
        T_TIMING, "Rendering rate limit", "-1=screen rate|0=unlimited", 1, 1000, SETTING_LIVE, .icon = ICON_SPEED },

    { SETTING_BOOL, "vsync", "false",
        "Vertical synchronization, enable if you get tearing\n"
        "Note: vsync=true can fix tearing but it will cause input lag",
        T_TIMING, "Vsync", BOOL_TF, 0, 0, SETTING_LIVE, .icon = ICON_SYNC },

    { SETTING_INT, "emulated_refresh", "auto",
        "The refresh rate of the emulated display, in Hz\n"
        "auto = the rate the game asks for, or 60 if it doesn't ask\n"
        STR(VBEAM_MIN_HZ) "-" STR(VBEAM_MAX_HZ) " = always this rate; CRTs of the era ran at 60, 70, 72, 75 or 85\n"
        "Note: The game sees this rate, and its flips and waits for the vertical blank follow it.\n"
        "  Your monitor's own rate stays as it is",
        T_TIMING, "Emulated refresh rate", "auto|60|70|72|75|85", VBEAM_MIN_HZ, VBEAM_MAX_HZ, SETTING_LIVE, .icon = ICON_AV_TIMER },

    { SETTING_BOOL, "adjmouse", "true",
        "Automatic mouse sensitivity scaling\n"
        "Note: Sensitivity will be adjusted according to the size of the picture",
        T_OTHER, "Scale the mouse", BOOL_TF, 0, 0, SETTING_LIVE, .icon = ICON_MOUSE },

    { SETTING_CHOICE, "shader", "crt-auto",
        "The shader, as in DOSBox Staging\n"
        "crt-auto = CRT shading for VGA, chosen by the number of screen pixels per line: sharp below two,\n"
        "  then vga-1080p, then crt-hyllian (DOSBox's vga-1440p and vga-4k presets)\n"
        "sharp = sharp pixels, smoothed only where the scaling isn't a whole number\n"
        "nearest = nearest neighbour\n"
        "bilinear = smooth",
        T_PICTURE, "Shader", "crt-auto|sharp|nearest|bilinear", 0, 0, SETTING_PICTURE, .icon = ICON_GRADIENT },

    INI_TEXT(
        "; The settings below have DOSBox Staging's names and values, and its defaults for VGA, so a DOSBox config's\n"
        "; [render] values can be copied here ('auto' too). An invalid value gets the default. They apply with every shader\n"
        "\n"),

    { SETTING_CHOICE, "aspect", "auto",
        "Aspect ratio correction\n"
        "auto, on = show 320x200 and 640x400 at 4:3, as on a CRT monitor; other modes have square pixels\n"
        "  (1280x1024 too, unlike in DOSBox, which shows it at 4:3)\n"
        "square-pixels, off = no correction; 320x200 and 640x400 look squashed\n"
        "stretch = take the shape from the viewport (see 'viewport'), for example to fill a 16:9 screen",
        T_PICTURE, "Aspect ratio", "auto|on|square-pixels|off|stretch", 0, 0, SETTING_PICTURE, .icon = ICON_ASPECT_RATIO },

    { SETTING_CHOICE, "integer_scaling", "auto",
        "Keep the scaling factor a whole number, so the picture still fits in the viewport\n"
        "auto = vertical, with 3.5x and 4.5x also allowed, and none above 5x\n"
        "vertical = whole numbers vertically\n"
        "horizontal = whole numbers horizontally\n"
        "off = fill the viewport",
        T_PICTURE, "Integer scaling", "auto|vertical|horizontal|off", 0, 0, SETTING_PICTURE, .icon = ICON_GRID_ON },

    { SETTING_STRING, "viewport", "fit",
        "The largest area the picture may use\n"
        "fit = the window or screen\n"
        "WxH = a size in logical units (e.g. 960x720; with display scaling at 150%, 1440x1080 pixels)\n"
        "WxHpx = a size in pixels (e.g. 1440x1080px)\n"
        "N% = a percentage of the desktop (e.g. 80%)\n"
        "  These must fit on the desktop (otherwise 'fit' is used) and shrink to fit a smaller window\n"
        "relative H% V% = a 4:3 area fitted to the window, scaled by H and V (20% to 300%). It may go past the\n"
        "  window's edges, like a CRT's stretch controls. Use it with 'aspect=stretch' for other shapes",
        T_PICTURE, "Viewport", "fit", 0, 0, SETTING_PICTURE, .icon = ICON_CROP_FREE },

    { SETTING_BOOL, "image_adjustments", "on",
        "Colour adjustments, as on a CRT monitor; off = the game's raw colours",
        T_COLOUR, "Colour adjustments", BOOL_ONOFF, 0, 0, SETTING_PICTURE, .flags = SETTING_GATE, .icon = ICON_TONALITY },

    OSD_HEADING(T_COLOUR, "Monitor", "The emulated monitor: its phosphors and its knobs"),

    { SETTING_CHOICE, "crt_color_profile", "p22",
        "CRT colour profile: the colours of the emulated monitor's phosphors\n"
        "none = the game's colours as they are\n"
        "ebu = EBU, the European TV standard\n"
        "p22 = P22, the phosphors of most VGA monitors (DOSBox's auto for VGA)\n"
        "smpte-c = SMPTE C, the American TV standard\n"
        "philips = a Philips monitor; meant for 'color_temperature=6500' and a wide gamut (DCI-P3) display\n"
        "trinitron = a Sony Trinitron; meant for 'color_temperature=6500' and a wide gamut (DCI-P3) display",
        T_COLOUR, "CRT colour profile", "none|ebu|p22|smpte-c|philips|trinitron", 0, 0, SETTING_PICTURE, .icon = ICON_TV },

    { SETTING_INT, "brightness", "45",
        "Brightness (0 to 100), the monitor's black point; higher raises the blacks",
        T_COLOUR, "Brightness", NULL, 0, 100, SETTING_PICTURE, .icon = ICON_BRIGHTNESS_6 },

    { SETTING_INT, "contrast", "65",
        "Contrast (0 to 100), the monitor's white point; higher also raises the blacks",
        T_COLOUR, "Contrast", NULL, 0, 100, SETTING_PICTURE, .icon = ICON_CONTRAST },

    { SETTING_INT, "gamma", "0",
        "Gamma (-50 to 50), on top of the emulated monitor's own",
        T_COLOUR, "Gamma", NULL, -50, 50, SETTING_PICTURE, .icon = ICON_EXPOSURE },

    OSD_HEADING(T_COLOUR, "Colour balance", "The monitor's white: its colour temperature and the gain of each colour"),

    { SETTING_INT, "color_temperature", "7800",
        "Colour temperature in Kelvin (3000 to 10000); 6500 is neutral, lower is warmer.\n"
        "  VGA monitors were bluish, about 7800 (DOSBox's auto for VGA)",
        T_COLOUR, "Colour temperature", NULL, 3000, 10000, SETTING_PICTURE, .step = 100, .icon = ICON_THERMOSTAT },

    { SETTING_INT, "color_temperature_luma_preserve", "0",
        "Keep the brightness when changing the colour temperature (0 to 100); 0 is the most accurate",
        T_COLOUR, "Keep brightness", NULL, 0, 100, SETTING_PICTURE, .icon = ICON_BRIGHTNESS_MEDIUM },

    { SETTING_INT, "red_gain", "100",
        "Red, green and blue gain (0 to 200); 100 = unchanged",
        T_COLOUR, "RGB gain", NULL, 0, 200, SETTING_PICTURE, .flags = SETTING_PACKED, .icon = ICON_COLOR_LENS, .tint = 0xDB4D4D, .tint_name = "Red" },

    { SETTING_INT, "green_gain", "100", NULL,
        T_COLOUR, "Green gain", NULL, 0, 200, SETTING_PICTURE, .flags = SETTING_PACKED | SETTING_JOINED, .tint = 0x59BF61, .tint_name = "Green" },

    { SETTING_INT, "blue_gain", "100", NULL,
        T_COLOUR, "Blue gain", NULL, 0, 200, SETTING_PICTURE, .flags = SETTING_JOINED, .tint = 0x668FF5, .tint_name = "Blue" },

    OSD_HEADING(T_COLOUR, "The game's colours", "Applied to the game's colours before the monitor"),

    { SETTING_INT, "digital_contrast", "0",
        "Digital contrast (-50 to 50), applied to the game's colours before the monitor",
        T_COLOUR, "Digital contrast", NULL, -50, 50, SETTING_PICTURE, .icon = ICON_FILTER_B_AND_W },

    { SETTING_INT, "saturation", "0",
        "Saturation (-50 to 50), applied to the game's colours",
        T_COLOUR, "Saturation", NULL, -50, 50, SETTING_PICTURE, .icon = ICON_INVERT_COLORS },

    { SETTING_INT, "black_level", "0",
        "Black level raise (0 to 100), before brightness and contrast (DOSBox's auto is 0 for VGA)",
        T_COLOUR, "Black level", NULL, 0, 100, SETTING_PICTURE, .icon = ICON_BRIGHTNESS_LOW },

    { SETTING_CHOICE, "renderer", "auto",
        "Renderer\n"
        "auto = Direct3D 11, or OpenGL if Direct3D 11 doesn't work\n"
        "direct3d11, opengl = this one first, the other if it doesn't work\n"
        "Note: When neither works, DirectCRT steps aside for Windows' own DirectDraw",
        T_DISPLAY, "Renderer", "auto|direct3d11|opengl", 0, 0, SETTING_LIVE, .icon = ICON_MEMORY },

    { SETTING_BOOL, "devmode", "false",
        "Developer mode (don't lock the cursor)",
        T_OTHER, "Developer mode", BOOL_TF, 0, 0, SETTING_LIVE, .icon = ICON_CODE },

    { SETTING_INT, "anti_aliased_fonts_min_size", "13",
        "Disable font smoothing for fonts that are smaller than size X",
        T_OTHER, "Smooth fonts from size", NULL, 0, 100, SETTING_LIVE, .icon = ICON_TEXT_FIELDS },

    { SETTING_STRING, "screenshotdir", ".\\Screenshots\\",
        "Where should screenshots be saved",
        T_OTHER, "Screenshot folder", NULL, 0, 0, SETTING_LIVE, .icon = ICON_FOLDER },

    { SETTING_BOOL, "status_osd", "off",
        "The status OSD: the game's resolution, the size and scaling of the picture, the shader, the frame rate\n"
        "and the frame times. Screenshots are without it\n"
        "Alt+F11 (see 'keystatusosd') shows and hides it, and saves that here, so it's the same the next time\n"
        "  the game starts",
        T_OVERLAY, "Status OSD", BOOL_ONOFF, 0, 0, SETTING_LIVE, .icon = ICON_INSIGHTS },

    { SETTING_CHOICE, "status_osd_size", "medium",
        "The size of the status OSD's text, times the display scale; at most a 22nd of the picture's height\n"
        "small = 13 pixels\n"
        "medium = 16 pixels\n"
        "large = 20 pixels",
        T_OVERLAY, "Status OSD size", "small|medium|large", 0, 0, SETTING_LIVE, .icon = ICON_FORMAT_SIZE },

    { SETTING_CHOICE, "status_osd_detail", "basic",
        "basic = three lines\n"
        "full = everything, with a graph of the frame times",
        T_OVERLAY, "Status OSD detail", "basic|full", 0, 0, SETTING_LIVE, .icon = ICON_NOTES },

    { SETTING_CHOICE, "status_osd_position", "top-left",
        "The corner of the screen or window the status OSD is in",
        T_OVERLAY, "Status OSD position", "top-left|top-right|bottom-left|bottom-right", 0, 0, SETTING_LIVE, .icon = ICON_PICTURE_IN_PICTURE },

    { SETTING_INT, "status_osd_opacity", "75",
        "How opaque the status OSD's background is, in percent (0 to 100); the text always stays solid",
        T_OVERLAY, "Status OSD opacity", NULL, 0, 100, SETTING_LIVE, .step = 5, .icon = ICON_OPACITY },

    INI_TEXT(
        "\n"
        "\n"
        "; ### Compatibility settings ###\n"
        "; Use the following settings in case there are any issues with the game\n"
        "\n"
        "\n"),

    { SETTING_BOOL, "noactivateapp", "false",
        "Hide WM_ACTIVATEAPP and WM_NCACTIVATE messages to prevent problems on alt+tab",
        T_COMPAT, "Hide activation messages", BOOL_TF, .icon = ICON_VISIBILITY_OFF },

    { SETTING_INT, "maxgameticks", "0",
        "Max game ticks per second\n"
        "0 = the emulated display's vertical blank (see 'emulated_refresh')\n"
        "-2 = your monitor's vertical blank\n"
        "-1 = disabled\n"
        "1-1000 = custom game speed\n"
        "Note: With 0, a flip is shown at the next vertical blank, and drawing into the picture still on screen waits for it\n"
        "Note: Can be used to slow down a too fast running game, fix flickering or too fast animations\n"
        "Note: Usually one of the following values will work: 60 / 30 / 25 / 20 / 15 (lower value = slower game speed)",
        T_TIMING, "Game speed (maxgameticks)",
        "0=the emulated vertical blank|-2=your monitor's vertical blank|-1=disabled", 1, 1000, .icon = ICON_SLOW_MOTION_VIDEO },

    { SETTING_CHOICE, "limiter_type", "0",
        "Method that should be used to limit game ticks (maxgameticks=)\n"
        "0 = Automatic\n"
        "1 = TestCooperativeLevel\n"
        "2 = BltFast\n"
        "3 = Unlock\n"
        "4 = PeekMessage",
        T_TIMING, "Game speed method", "0=Automatic|1=TestCooperativeLevel|2=BltFast|3=Unlock|4=PeekMessage", .icon = ICON_TIMELAPSE },

    { SETTING_INT, "minfps", "0",
        "Force minimum FPS\n"
        "0 = disabled\n"
        "-1 = use the 'maxfps' value\n"
        "-2 = same as -1 but force full redraw\n"
        "1-1000 = custom FPS\n"
        "Note: Set this to a low value such as 5 or 10 if some parts of the game are not being displayed (e.g. menus or loading screens)",
        T_TIMING, "Minimum frame rate", "0=disabled|-1=the rendering rate limit|-2=the limit, full redraw", 1, 1000, .icon = ICON_HOURGLASS_BOTTOM },

    { SETTING_BOOL, "nonexclusive", "true",
        "Disable fullscreen-exclusive mode for the opengl renderer\n"
        "Note: Can be used in case some GUI elements like buttons/textboxes/videos/etc.. are invisible",
        T_COMPAT, "OpenGL: not exclusive", BOOL_TF, .icon = ICON_FULLSCREEN_EXIT },

    { SETTING_BOOL, "singlecpu", "true",
        "Force CPU0 affinity, avoids crashes/freezing, *might* have a performance impact\n"
        "Note: Disable this if the game is not running smooth or there are sound issues",
        T_COMPAT, "One CPU core", BOOL_TF, .icon = ICON_DEVELOPER_BOARD },

    { SETTING_CHOICE, "resolutions", "0",
        "Available display resolutions\n"
        "0 = Small list\n"
        "1 = Very small list; try it if the game is crashing on startup\n"
        "2 = Full list; try it if your chosen resolution is not working or does not show up in the list",
        T_COMPAT, "Display modes offered", "0=Small list|1=Very small list|2=Full list", .icon = ICON_VIEW_LIST },

    { SETTING_CHOICE, "fixchilds", "2",
        "Child window handling\n"
        "0 = Disabled\n"
        "1 = Display top left\n"
        "2 = Display top left + repaint\n"
        "3 = Hide\n"
        "4 = Display top left + hide\n"
        "Note: Disables upscaling if a child window was detected (to ensure the game is fully playable, may look weird though)",
        T_COMPAT, "Child windows",
        "0=Disabled|1=Display top left|2=Display top left + repaint|3=Hide|4=Display top left + hide", .icon = ICON_FILTER_NONE },

    { SETTING_BOOL, "hook_peekmessage", "false",
        "Enable the following setting if your cursor doesn't lock to the window or it doesn't work properly when upscaling is enabled",
        T_COMPAT, "Hook PeekMessage", BOOL_TF, .icon = ICON_CENTER_FOCUS_STRONG },

    { SETTING_CHOICE, "windowed_bpp", "0",
        "Bit depth of the screen for games that run in a window (normal cooperative level)\n"
        "0 = the desktop's, as Windows' own DirectDraw does (32 today)\n"
        "16 = 16-bit\n"
        "Note: Set this to 16 if a windowed game shows garbled colours",
        T_COMPAT, "Bit depth in a window", "0=the desktop's|16=16-bit", .icon = ICON_COLORIZE },

    INI_TEXT(
        "\n"
        "; Undocumented compatibility settings - These will probably not solve your problem, you should rather focus on the settings above\n"),

    { SETTING_BOOL, "fix_alt_key_stuck", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "game_handles_close", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "fix_not_responding", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "no_compat_warning", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_INT, "guard_lines", "200", NULL, NULL, NULL, NULL, 0, 10000, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_INT, "max_resolutions", "0", NULL, NULL, NULL, NULL, 0, 10000, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "lock_surfaces", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "flipclear", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "rgb555", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "no_dinput_hook", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "center_cursor_fix", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_STRING, "fake_mode", "640x480x32", NULL, NULL, NULL, NULL, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED | SETTING_COMMENTED },
    { SETTING_BOOL, "lock_mouse_top_left", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_STRING, "win_version", "95", NULL, NULL, NULL, NULL, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED | SETTING_COMMENTED },
    { SETTING_INT, "hook", "4", NULL, NULL, NULL, NULL, 0, 4, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "limit_gdi_handles", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "remove_menu", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_INT, "refresh_rate", "0", NULL, NULL, NULL, NULL, 0, 1000, SETTING_RESTART, .flags = SETTING_PACKED },
    { SETTING_BOOL, "terminate_process", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, SETTING_RESTART, .flags = SETTING_PACKED },

    INI_TEXT(
        "\n"
        "\n"
        "\n"
        "; ### Hotkeys ###\n"
        "; Use the following settings to configure your hotkeys, 0x00 = disabled\n"
        "; Virtual-Key Codes: https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes\n"
        "\n"
        "\n"),

    { SETTING_KEY, "keytogglefullscreen", "0x0D",
        "Switch between windowed and fullscreen mode = [Alt] + ???",
        T_KEYS, "Fullscreen or a window: Alt +", NULL, 0, 0, SETTING_LIVE, .icon = ICON_FULLSCREEN },

    { SETTING_KEY, "keytogglefullscreen2", "0x00",
        "Switch between windowed and fullscreen mode (single key) = ???",
        T_KEYS, "Fullscreen or a window", NULL, 0, 0, SETTING_LIVE, .icon = ICON_FULLSCREEN },

    { SETTING_KEY, "keytogglemaximize", "0x22",
        "Maximize window = [Alt] + ???",
        T_KEYS, "Maximize: Alt +", NULL, 0, 0, SETTING_LIVE, .icon = ICON_OPEN_IN_FULL },

    { SETTING_KEY, "keytogglemaximize2", "0x00",
        "Maximize window (single key) = ???",
        T_KEYS, "Maximize", NULL, 0, 0, SETTING_LIVE, .icon = ICON_OPEN_IN_FULL },

    { SETTING_KEY, "keyunlockcursor1", "0x09",
        "Unlock cursor 1 = [Ctrl] + ???",
        T_KEYS, "Unlock the cursor: Ctrl +", NULL, 0, 0, SETTING_LIVE, .icon = ICON_LOCK_OPEN },

    { SETTING_KEY, "keyunlockcursor2", "0xA3",
        "Unlock cursor 2 = [Right Alt] + ???",
        T_KEYS, "Unlock the cursor: Right Alt +", NULL, 0, 0, SETTING_LIVE, .icon = ICON_LOCK_OPEN },

    { SETTING_BOOL, "unlockcursor_middleclick", "false",
        "Unlock cursor with a middle click (the game doesn't get the click; leave off for games that use the middle button)",
        T_KEYS, "Unlock the cursor: middle click", BOOL_TF, 0, 0, SETTING_LIVE, .icon = ICON_MOUSE },

    { SETTING_KEY, "keyscreenshot", "0x2C",
        "Screenshot: saves the game's own picture (_raw.png) and what's on screen (_crt.png)",
        T_KEYS, "Screenshot", NULL, 0, 0, SETTING_LIVE, .icon = ICON_PHOTO_CAMERA },

    { SETTING_KEY, "keystatusosd", "0x7A",
        "Show or hide the status OSD = [Alt] + ???",
        T_KEYS, "Status OSD: Alt +", NULL, 0, 0, SETTING_LIVE, .icon = ICON_INSIGHTS },

    { SETTING_KEY, "keysettingsosd", "0x7B",
        "Open or close the settings OSD = [Alt] + ???\n"
        "Note: While it's open the game keeps running, but it gets no keyboard or mouse input",
        T_KEYS, "Settings OSD: Alt +", NULL, 0, 0, SETTING_LIVE, .icon = ICON_SETTINGS },

};

int settings_count(void)
{
    return (int)(sizeof(g_settings) / sizeof(g_settings[0]));
}

const SETTING* settings_get(int i)
{
    return i >= 0 && i < settings_count() ? &g_settings[i] : NULL;
}

int settings_index(const char* key)
{
    for (int i = 0; key && i < settings_count(); i++)
    {
        if (g_settings[i].key && _stricmp(g_settings[i].key, key) == 0)
            return i;
    }

    return -1;
}

const SETTING* settings_find(const char* key)
{
    return settings_get(settings_index(key));
}

const char* settings_tab(int i)
{
    return i >= 0 && i < (int)(sizeof(g_tabs) / sizeof(g_tabs[0])) ? g_tabs[i] : NULL;
}

static void settings_copy(char* buf, size_t size, const char* from, size_t len)
{
    if (!buf || !size)
        return;

    if (len >= size)
        len = size - 1;

    memcpy(buf, from, len);
    buf[len] = 0;
}

int settings_choice(const SETTING* s, int i, char* value, size_t value_size, char* label, size_t label_size)
{
    const char* p = s ? s->choices : NULL;

    for (; p && *p && i > 0; i--)
    {
        p = strchr(p, '|');
        p = p ? p + 1 : NULL;
    }

    if (!p || !*p)
        return FALSE;

    const char* end = strchr(p, '|');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    const char* eq = memchr(p, '=', len);

    if (eq)
    {
        settings_copy(value, value_size, p, eq - p);
        settings_copy(label, label_size, eq + 1, len - (eq - p) - 1);
    }
    else
    {
        settings_copy(value, value_size, p, len);
        settings_copy(label, label_size, p, len);
    }

    return TRUE;
}

int settings_help_line(const char** cursor, char* buf, size_t size)
{
    const char* p = cursor ? *cursor : NULL;

    if (!p || !*p || size == 0)
        return FALSE;

    size_t len = 0;
    buf[0] = 0;

    do
    {
        const char* end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        const char* from = p;

        /* "  more" goes on with " more" */
        if (len > 0)
        {
            from++;
            n--;
        }

        size_t copy = n < size - 1 - len ? n : size - 1 - len;
        memcpy(buf + len, from, copy);
        len += copy;
        buf[len] = 0;

        p = end ? end + 1 : from + n;
    } while (p[0] == ' ' && p[1] == ' ');

    *cursor = p;
    return TRUE;
}

/* Is value one of "a, b" (the part of a help line before " = ")? */
static int settings_in_list(const char* list, size_t list_len, const char* value)
{
    size_t value_len = strlen(value);
    const char* p = list;
    const char* end = list + list_len;

    while (p < end)
    {
        const char* comma = memchr(p, ',', end - p);
        const char* item_end = comma ? comma : end;

        while (p < item_end && *p == ' ')
            p++;

        if ((size_t)(item_end - p) == value_len && _strnicmp(p, value, value_len) == 0)
            return TRUE;

        p = comma ? comma + 1 : end;
    }

    return FALSE;
}

int settings_choice_help(const SETTING* s, const char* value, char* buf, size_t size)
{
    if (!s || !s->help || size == 0)
        return FALSE;

    char line[1024];
    const char* cursor = s->help;
    size_t len = 0;
    buf[0] = 0;

    while (settings_help_line(&cursor, line, sizeof(line)))
    {
        const char* eq = strstr(line, " = ");

        if (!eq)
            continue;

        size_t left = eq - line;
        const char* text = NULL;

        if (value)
        {
            if (settings_in_list(line, left, value))
                text = eq + 3;
        }
        else if (left <= 20)
        {
            /* A format such as "WxH" or "24-240", not one of the choices */
            int named = FALSE;
            char v[64], l[64];

            for (int i = 0; !named && settings_choice(s, i, v, sizeof(v), l, sizeof(l)); i++)
                named = settings_in_list(line, left, v);

            if (!named)
                text = line;
        }

        if (!text)
            continue;

        size_t n = strlen(text);

        if (len + n + 2 > size)
            break;

        if (len > 0)
            buf[len++] = '\n';

        /* "auto = what the game asks for" -> "What the game asks for" */
        memcpy(buf + len, text, n + 1);

        if (value && buf[len] >= 'a' && buf[len] <= 'z')
            buf[len] = (char)(buf[len] - 'a' + 'A');

        len += n;
    }

    return len > 0;
}

void settings_write_ini(FILE* fh)
{
    for (int i = 0; i < settings_count(); i++)
    {
        const SETTING* s = &g_settings[i];

        if (s->kind == SETTING_TEXT)
        {
            fputs(s->help, fh);
            continue;
        }

        if (s->kind == SETTING_HEADING)
            continue;

        for (const char* line = s->help; line && *line;)
        {
            const char* end = strchr(line, '\n');
            size_t len = end ? (size_t)(end - line) : strlen(line);

            fputs("; ", fh);
            fwrite(line, 1, len, fh);
            fputs("\n", fh);

            line = end ? end + 1 : NULL;
        }

        fprintf(fh, "%s%s=%s\n", (s->flags & SETTING_COMMENTED) ? ";" : "", s->key, s->value);

        if (!(s->flags & SETTING_PACKED))
            fputs("\n", fh);
    }
}
