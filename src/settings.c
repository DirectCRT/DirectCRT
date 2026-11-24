#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"

/* The ini's lines and the OSD's tabs. Keep the ini's wording in the help: the
   OSD shows it as it is */

#define INI_TEXT(t) { SETTING_TEXT, NULL, NULL, t }

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
        "; Changes to the picture and colour settings (from 'shader' to 'blue_gain'), emulated_refresh, remember_window,\n"
        "; the status OSD and the hotkeys apply while the game runs, when this file is saved; others need a restart.\n"
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
        T_DISPLAY, "Display", "auto|window|fullscreen|original" },

    { SETTING_STRING, "window_size", "default",
        "The size of the window's contents, without its title bar and borders\n"
        "default = medium\n"
        "small, medium, large = 4:3 sizes of 50%, 74% and 90% of the desktop height\n"
        "unscaled = the game's own resolution at 1:1, for games that misbehave when scaled\n"
        "WxH = a size in logical units (e.g. 1280x960; with display scaling at 150%, 1920x1440 pixels)\n"
        "WxHpx = a size in pixels (e.g. 1920x1440px)\n"
        "Note: The settings for the picture (aspect, integer_scaling, viewport) place it in the window, as in fullscreen",
        T_DISPLAY, "Window size", "default|small|medium|large|unscaled" },

    { SETTING_STRING, "window_position", "auto",
        "Where the window goes\n"
        "auto = centred on the screen, or where the game put its own window\n"
        "X,Y = the top left corner in logical units; X,Ypx = in pixels; X,Y% = a percentage of the desktop (e.g. 10,5%)",
        T_DISPLAY, "Window position", "auto" },

    { SETTING_BOOL, "window_decorations", "on",
        "Show the window's title bar and borders",
        T_DISPLAY, "Title bar and borders", BOOL_ONOFF },

    { SETTING_BOOL, "window_resizable", "on",
        "Let the user resize and maximize the window",
        T_DISPLAY, "Resizable window", BOOL_ONOFF },

    { SETTING_CHOICE, "remember_window", "off",
        "Save the window's size and position when the game exits, for the next start\n"
        "off = don't save; global = in the [directcrt] section; game = in the game's own section\n"
        "Note: Alt+Enter's state isn't saved; every start uses 'display'",
        T_DISPLAY, "Remember the window", "off|global|game", 0, 0, SETTING_LIVE },

    { SETTING_INT, "maxfps", "-1",
        "Real rendering rate, -1 = screen rate, 0 = unlimited, n = cap\n"
        "Note: Does not have an impact on the game speed, to limit your game speed use 'maxgameticks='",
        T_TIMING, "Rendering rate limit", "-1=screen rate|0=unlimited", 1, 1000 },

    { SETTING_BOOL, "vsync", "false",
        "Vertical synchronization, enable if you get tearing\n"
        "Note: vsync=true can fix tearing but it will cause input lag",
        T_TIMING, "Vsync", BOOL_TF },

    { SETTING_INT, "emulated_refresh", "auto",
        "The refresh rate of the emulated display, in Hz\n"
        "auto = the rate the game asks for, or 60 if it doesn't ask\n"
        "24-240 = always this rate; CRTs of the era ran at 60, 70, 72, 75 or 85\n"
        "Note: The game sees this rate, and its flips and waits for the vertical blank follow it.\n"
        "  Your monitor's own rate stays as it is",
        T_TIMING, "Emulated refresh rate", "auto|60|70|72|75|85", 24, 240, SETTING_LIVE },

    { SETTING_BOOL, "adjmouse", "true",
        "Automatic mouse sensitivity scaling\n"
        "Note: Sensitivity will be adjusted according to the size of the picture",
        T_OTHER, "Scale the mouse", BOOL_TF },

    { SETTING_CHOICE, "shader", "crt-auto",
        "The shader, as in DOSBox Staging\n"
        "crt-auto = CRT shading for VGA, chosen by the number of screen pixels per line: sharp below two,\n"
        "  then vga-1080p, then crt-hyllian (DOSBox's vga-1440p and vga-4k presets)\n"
        "sharp = sharp pixels, smoothed only where the scaling isn't a whole number\n"
        "nearest = nearest neighbour\n"
        "bilinear = smooth",
        T_PICTURE, "Shader", "crt-auto|sharp|nearest|bilinear", 0, 0, SETTING_PICTURE },

    INI_TEXT(
        "; The settings below have DOSBox Staging's names, values and defaults, so a DOSBox config's [render]\n"
        "; values can be copied here. An invalid value gets the default. They apply with every shader\n"
        "\n"),

    { SETTING_CHOICE, "aspect", "auto",
        "Aspect ratio correction\n"
        "auto, on = show 320x200 and 640x400 at 4:3, as on a CRT monitor; other modes have square pixels\n"
        "  (1280x1024 too, unlike in DOSBox, which shows it at 4:3)\n"
        "square-pixels, off = no correction; 320x200 and 640x400 look squashed\n"
        "stretch = take the shape from the viewport (see 'viewport'), for example to fill a 16:9 screen",
        T_PICTURE, "Aspect ratio", "auto|on|square-pixels|off|stretch", 0, 0, SETTING_PICTURE },

    { SETTING_CHOICE, "integer_scaling", "auto",
        "Keep the scaling factor a whole number, so the picture still fits in the viewport\n"
        "auto = with crt-auto: vertical, with 3.5x and 4.5x also allowed, and none above 5x; other shaders: off\n"
        "vertical = whole numbers vertically; horizontal = whole numbers horizontally; off = fill the viewport",
        T_PICTURE, "Integer scaling", "auto|vertical|horizontal|off", 0, 0, SETTING_PICTURE },

    { SETTING_STRING, "viewport", "fit",
        "The largest area the picture may use\n"
        "fit = the window or screen\n"
        "WxH = a size in logical units (e.g. 960x720; with display scaling at 150%, 1440x1080 pixels)\n"
        "WxHpx = a size in pixels (e.g. 1440x1080px); N% = a percentage of the desktop (e.g. 80%)\n"
        "  These must fit on the desktop (otherwise 'fit' is used) and shrink to fit a smaller window\n"
        "relative H% V% = a 4:3 area fitted to the window, scaled by H and V (20% to 300%). It may go past the\n"
        "  window's edges, like a CRT's stretch controls. Use it with 'aspect=stretch' for other shapes",
        T_PICTURE, "Viewport", "fit", 0, 0, SETTING_PICTURE },

    { SETTING_BOOL, "image_adjustments", "on",
        "Colour adjustments, as on a CRT monitor; off = the game's raw colours",
        T_COLOUR, "Colour adjustments", BOOL_ONOFF, 0, 0, SETTING_PICTURE },

    { SETTING_CHOICE, "crt_color_profile", "auto",
        "CRT colour profile: auto (p22 for VGA), none, ebu, p22, smpte-c, philips, trinitron\n"
        "Note: philips and trinitron are meant for 'color_temperature=6500' and a wide gamut (DCI-P3) display",
        T_COLOUR, "CRT colour profile", "auto|none|ebu|p22|smpte-c|philips|trinitron", 0, 0, SETTING_PICTURE },

    { SETTING_INT, "brightness", "45",
        "Brightness (0 to 100), the monitor's black point; higher raises the blacks",
        T_COLOUR, "Brightness", NULL, 0, 100, SETTING_PICTURE },

    { SETTING_INT, "contrast", "65",
        "Contrast (0 to 100), the monitor's white point; higher also raises the blacks",
        T_COLOUR, "Contrast", NULL, 0, 100, SETTING_PICTURE },

    { SETTING_INT, "gamma", "0",
        "Gamma (-50 to 50), on top of the emulated monitor's own",
        T_COLOUR, "Gamma", NULL, -50, 50, SETTING_PICTURE },

    { SETTING_INT, "digital_contrast", "0",
        "Digital contrast (-50 to 50), applied to the game's colours before the monitor",
        T_COLOUR, "Digital contrast", NULL, -50, 50, SETTING_PICTURE },

    { SETTING_INT, "black_level", "auto",
        "Black level raise (0 to 100), before brightness and contrast; auto = 0 for VGA",
        T_COLOUR, "Black level", "auto", 0, 100, SETTING_PICTURE },

    { SETTING_INT, "saturation", "0",
        "Saturation (-50 to 50), applied to the game's colours",
        T_COLOUR, "Saturation", NULL, -50, 50, SETTING_PICTURE },

    { SETTING_INT, "color_temperature", "auto",
        "Colour temperature in Kelvin (3000 to 10000); 6500 is neutral, lower is warmer; auto = 7800 for VGA",
        T_COLOUR, "Colour temperature", "auto", 3000, 10000, SETTING_PICTURE },

    { SETTING_INT, "color_temperature_luma_preserve", "0",
        "Keep the brightness when changing the colour temperature (0 to 100); 0 is the most accurate",
        T_COLOUR, "Keep brightness", NULL, 0, 100, SETTING_PICTURE },

    { SETTING_INT, "red_gain", "100",
        "Red, green and blue gain (0 to 200); 100 = unchanged",
        T_COLOUR, "Red gain", NULL, 0, 200, SETTING_PICTURE, SETTING_PACKED },

    { SETTING_INT, "green_gain", "100", NULL,
        T_COLOUR, "Green gain", NULL, 0, 200, SETTING_PICTURE, SETTING_PACKED },

    { SETTING_INT, "blue_gain", "100", NULL,
        T_COLOUR, "Blue gain", NULL, 0, 200, SETTING_PICTURE },

    { SETTING_CHOICE, "renderer", "auto",
        "Renderer\n"
        "auto = Direct3D 11, or OpenGL if Direct3D 11 doesn't work\n"
        "direct3d11, opengl = this one first, the other if it doesn't work\n"
        "Note: When neither works, DirectCRT steps aside for Windows' own DirectDraw",
        T_DISPLAY, "Renderer", "auto|direct3d11|opengl" },

    { SETTING_BOOL, "devmode", "false",
        "Developer mode (don't lock the cursor)",
        T_OTHER, "Developer mode", BOOL_TF },

    { SETTING_INT, "anti_aliased_fonts_min_size", "13",
        "Disable font smoothing for fonts that are smaller than size X",
        T_OTHER, "Smooth fonts from size", NULL, 0, 100 },

    { SETTING_STRING, "screenshotdir", ".\\Screenshots\\",
        "Where should screenshots be saved",
        T_OTHER, "Screenshot folder" },

    { SETTING_CHOICE, "status_osd_size", "medium",
        "The status OSD, shown and hidden with Alt+F11 (see 'keystatusosd'): the game's resolution, the size\n"
        "and scaling of the picture, the shader, the frame rate and the frame times. Screenshots are without it\n"
        "small, medium, large",
        T_OVERLAY, "Status OSD size", "small|medium|large", 0, 0, SETTING_LIVE },

    { SETTING_CHOICE, "status_osd_detail", "basic",
        "basic = three lines; full = everything, with a graph of the frame times",
        T_OVERLAY, "Status OSD detail", "basic|full", 0, 0, SETTING_LIVE },

    { SETTING_CHOICE, "status_osd_position", "top-left",
        "top-left, top-right, bottom-left, bottom-right",
        T_OVERLAY, "Status OSD position", "top-left|top-right|bottom-left|bottom-right", 0, 0, SETTING_LIVE },

    INI_TEXT(
        "\n"
        "\n"
        "; ### Compatibility settings ###\n"
        "; Use the following settings in case there are any issues with the game\n"
        "\n"
        "\n"),

    { SETTING_BOOL, "noactivateapp", "false",
        "Hide WM_ACTIVATEAPP and WM_NCACTIVATE messages to prevent problems on alt+tab",
        T_COMPAT, "Hide activation messages", BOOL_TF },

    { SETTING_INT, "maxgameticks", "0",
        "Max game ticks per second, possible values: -1 = disabled, -2 = your monitor's vertical blank,\n"
        "  0 = the emulated display's vertical blank (see 'emulated_refresh'), 1-1000 = custom game speed\n"
        "Note: With 0, a flip is shown at the next vertical blank, and drawing into the picture still on screen waits for it\n"
        "Note: Can be used to slow down a too fast running game, fix flickering or too fast animations\n"
        "Note: Usually one of the following values will work: 60 / 30 / 25 / 20 / 15 (lower value = slower game speed)",
        T_TIMING, "Game speed (maxgameticks)",
        "0=the emulated vertical blank|-2=your monitor's vertical blank|-1=disabled", 1, 1000 },

    { SETTING_CHOICE, "limiter_type", "0",
        "Method that should be used to limit game ticks (maxgameticks=): 0 = Automatic, 1 = TestCooperativeLevel, 2 = BltFast, 3 = Unlock, 4 = PeekMessage",
        T_TIMING, "Game speed method", "0=Automatic|1=TestCooperativeLevel|2=BltFast|3=Unlock|4=PeekMessage" },

    { SETTING_INT, "minfps", "0",
        "Force minimum FPS, possible values: 0 = disabled, -1 = use 'maxfps=' value, -2 = same as -1 but force full redraw, 1-1000 = custom FPS\n"
        "Note: Set this to a low value such as 5 or 10 if some parts of the game are not being displayed (e.g. menus or loading screens)",
        T_TIMING, "Minimum frame rate", "0=disabled|-1=the rendering rate limit|-2=the limit, full redraw", 1, 1000 },

    { SETTING_BOOL, "nonexclusive", "true",
        "Disable fullscreen-exclusive mode for the opengl renderer\n"
        "Note: Can be used in case some GUI elements like buttons/textboxes/videos/etc.. are invisible",
        T_COMPAT, "OpenGL: not exclusive", BOOL_TF },

    { SETTING_BOOL, "singlecpu", "true",
        "Force CPU0 affinity, avoids crashes/freezing, *might* have a performance impact\n"
        "Note: Disable this if the game is not running smooth or there are sound issues",
        T_COMPAT, "One CPU core", BOOL_TF },

    { SETTING_CHOICE, "resolutions", "0",
        "Available display resolutions, possible values: 0 = Small list, 1 = Very small list, 2 = Full list\n"
        "Note: Set this to 2 if your chosen resolution is not working or does not show up in the list\n"
        "Note: Set this to 1 if the game is crashing on startup",
        T_COMPAT, "Display modes offered", "0=Small list|1=Very small list|2=Full list" },

    { SETTING_CHOICE, "fixchilds", "2",
        "Child window handling, possible values: 0 = Disabled, 1 = Display top left, 2 = Display top left + repaint, 3 = Hide, 4 = Display top left + hide\n"
        "Note: Disables upscaling if a child window was detected (to ensure the game is fully playable, may look weird though)",
        T_COMPAT, "Child windows",
        "0=Disabled|1=Display top left|2=Display top left + repaint|3=Hide|4=Display top left + hide" },

    { SETTING_BOOL, "hook_peekmessage", "false",
        "Enable the following setting if your cursor doesn't lock to the window or it doesn't work properly when upscaling is enabled",
        T_COMPAT, "Hook PeekMessage", BOOL_TF },

    { SETTING_CHOICE, "windowed_bpp", "0",
        "Bit depth of the screen for games that run in a window (normal cooperative level)\n"
        "Possible values: 0 = the desktop's, as Windows' own DirectDraw does (32 today), 16 = 16-bit\n"
        "Note: Set this to 16 if a windowed game shows garbled colours",
        T_COMPAT, "Bit depth in a window", "0=the desktop's|16=16-bit" },

    INI_TEXT(
        "\n"
        "; Undocumented compatibility settings - These will probably not solve your problem, you should rather focus on the settings above\n"),

    { SETTING_BOOL, "fix_alt_key_stuck", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_BOOL, "game_handles_close", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_BOOL, "fix_not_responding", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_BOOL, "no_compat_warning", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_INT, "guard_lines", "200", NULL, NULL, NULL, NULL, 0, 10000, 0, SETTING_PACKED },
    { SETTING_INT, "max_resolutions", "0", NULL, NULL, NULL, NULL, 0, 10000, 0, SETTING_PACKED },
    { SETTING_BOOL, "lock_surfaces", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_BOOL, "flipclear", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_BOOL, "rgb555", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_BOOL, "no_dinput_hook", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_BOOL, "center_cursor_fix", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_STRING, "fake_mode", "640x480x32", NULL, NULL, NULL, NULL, 0, 0, 0, SETTING_PACKED | SETTING_COMMENTED },
    { SETTING_BOOL, "lock_mouse_top_left", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_STRING, "win_version", "95", NULL, NULL, NULL, NULL, 0, 0, 0, SETTING_PACKED | SETTING_COMMENTED },
    { SETTING_INT, "hook", "4", NULL, NULL, NULL, NULL, 0, 4, 0, SETTING_PACKED },
    { SETTING_BOOL, "limit_gdi_handles", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_BOOL, "remove_menu", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },
    { SETTING_INT, "refresh_rate", "0", NULL, NULL, NULL, NULL, 0, 1000, 0, SETTING_PACKED },
    { SETTING_BOOL, "terminate_process", "false", NULL, NULL, NULL, BOOL_TF, 0, 0, 0, SETTING_PACKED },

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
        T_KEYS, "Fullscreen or a window: Alt +", NULL, 0, 0, SETTING_LIVE },

    { SETTING_KEY, "keytogglefullscreen2", "0x00",
        "Switch between windowed and fullscreen mode (single key) = ???",
        T_KEYS, "Fullscreen or a window", NULL, 0, 0, SETTING_LIVE },

    { SETTING_KEY, "keytogglemaximize", "0x22",
        "Maximize window = [Alt] + ???",
        T_KEYS, "Maximize: Alt +", NULL, 0, 0, SETTING_LIVE },

    { SETTING_KEY, "keytogglemaximize2", "0x00",
        "Maximize window (single key) = ???",
        T_KEYS, "Maximize", NULL, 0, 0, SETTING_LIVE },

    { SETTING_KEY, "keyunlockcursor1", "0x09",
        "Unlock cursor 1 = [Ctrl] + ???",
        T_KEYS, "Unlock the cursor: Ctrl +", NULL, 0, 0, SETTING_LIVE },

    { SETTING_KEY, "keyunlockcursor2", "0xA3",
        "Unlock cursor 2 = [Right Alt] + ???",
        T_KEYS, "Unlock the cursor: Right Alt +", NULL, 0, 0, SETTING_LIVE },

    { SETTING_BOOL, "unlockcursor_middleclick", "false",
        "Unlock cursor with a middle click (the game doesn't get the click; leave off for games that use the middle button)",
        T_KEYS, "Unlock the cursor: middle click", BOOL_TF, 0, 0, SETTING_LIVE },

    { SETTING_KEY, "keyscreenshot", "0x2C",
        "Screenshot: saves the game's own picture (_raw.png) and what's on screen (_crt.png)",
        T_KEYS, "Screenshot", NULL, 0, 0, SETTING_LIVE },

    { SETTING_KEY, "keystatusosd", "0x7A",
        "Show or hide the status OSD = [Alt] + ???",
        T_KEYS, "Status OSD: Alt +", NULL, 0, 0, SETTING_LIVE },

    { SETTING_KEY, "keysettingsosd", "0x7B",
        "Open or close the settings OSD = [Alt] + ???\n"
        "Note: While it's open the game keeps running, but it gets no keyboard or mouse input",
        T_KEYS, "Settings OSD: Alt +", NULL, 0, 0, SETTING_LIVE },

    INI_TEXT(
        "\n"
        "\n"
        "; ### Settings for one game ###\n"
        "; A section named after a game's executable, without '.exe', overrides the settings above for that game.\n"
        "; Any setting can go in it. For example, for game.exe:\n"
        ";\n"
        "; [game]\n"
        "; maxgameticks=60\n"
        "\n"),
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
