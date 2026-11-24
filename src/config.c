#include <windows.h>
#include <stdio.h>
#include "fps_limiter.h"
#include "config.h"
#include "dd.h"
#include "render_ogl.h"
#include "hook.h"
#include "debug.h"
#include "dllmain.h"
#include "ini.h"
#include "settings.h"
#include "versionhelpers.h"

static void cfg_init();
static void cfg_create_ini();
static BOOL cfg_get_bool(LPCSTR key, BOOL default_value);
static int cfg_get_int(LPCSTR key, int default_value);
static DWORD cfg_get_string(LPCSTR key, LPCSTR default_value, LPSTR out_string, DWORD out_size);
static DWORD cfg_get_game_section(LPSTR buf, DWORD size);
static void cfg_load_crt_auto_settings(CRT_AUTO_SETTINGS* settings);
static void cfg_load_display_settings();
static void cfg_load_live_settings();
static void cfg_ini_stat(WIN32_FILE_ATTRIBUTE_DATA* data);
static int cfg_get_choice(LPCSTR key, const char* const* names, int count, int default_index);

#define GET_INT(a,b,c) a = cfg_get_int(b, c); TRACE("%s=%d\n", b, a)
#define GET_BOOL(a,b,c) a = cfg_get_bool(b, c); TRACE("%s=%s\n", b, a ? "true" : "false")
#define GET_STRING(a,b,c,d) cfg_get_string(a, b, c, d); TRACE("%s=%s\n", a, c)

CNCDDRAWCONFIG g_config =
    { .window_rect = {.left = -32000, .top = -32000, .right = 0, .bottom = 0 } };

/* directcrt.ini as last seen, for the live reload */
static WIN32_FILE_ATTRIBUTE_DATA g_ini_seen;
static BOOL g_ini_changed;

void cfg_load()
{
    cfg_init();
    cfg_ini_stat(&g_ini_seen);
    g_ini_changed = FALSE;

    /* Optional settings */

    cfg_load_display_settings();
    GET_INT(g_config.maxfps, "maxfps", -1);
    GET_BOOL(g_config.vsync, "vsync", FALSE);
    GET_BOOL(g_config.adjmouse, "adjmouse", TRUE);
    cfg_load_crt_auto_settings(&g_config.crt);
    GET_STRING("renderer", "auto", g_config.renderer, sizeof(g_config.renderer));
    const char* renderer = ds_legacy_renderer(g_config.renderer);
    if (renderer)
    {
        TRACE("     renderer=%s is no longer available, using %s\n", g_config.renderer, renderer);
        strncpy(g_config.renderer, renderer, sizeof(g_config.renderer) - 1);
    }
    GET_BOOL(g_config.devmode, "devmode", FALSE);
    GET_INT(g_config.anti_aliased_fonts_min_size, "anti_aliased_fonts_min_size", 13);
    GET_STRING("screenshotdir", ".\\Screenshots\\", g_config.screenshot_dir, sizeof(g_config.screenshot_dir));

    /* emulated_refresh, remember_window, the status OSD and the hotkeys */
    cfg_load_live_settings();

    /* Compatibility settings */

    GET_BOOL(g_config.noactivateapp, "noactivateapp", FALSE);
    GET_INT(g_config.maxgameticks, "maxgameticks", 0);
    GET_INT(g_config.limiter_type, "limiter_type", LIMIT_AUTO);
    GET_INT(g_config.minfps, "minfps", 0);
    GET_BOOL(g_config.nonexclusive, "nonexclusive", TRUE);
    GET_BOOL(g_config.singlecpu, "singlecpu", TRUE);
    GET_INT(g_config.resolutions, "resolutions", RESLIST_NORMAL);
    GET_INT(g_config.fixchilds, "fixchilds", FIX_CHILDS_DETECT_PAINT);
    GET_BOOL(g_config.hook_peekmessage, "hook_peekmessage", FALSE);
    GET_INT(g_config.windowed_bpp, "windowed_bpp", 0);

    /* Undocumented settings */

    GET_BOOL(g_config.fix_alt_key_stuck, "fix_alt_key_stuck", FALSE);
    GET_BOOL(GameHandlesClose, "game_handles_close", FALSE);
    GET_BOOL(g_config.fix_not_responding, "fix_not_responding", FALSE);
    GET_BOOL(g_config.no_compat_warning, "no_compat_warning", FALSE);
    GET_INT(g_config.guard_lines, "guard_lines", 200);
    GET_INT(g_config.max_resolutions, "max_resolutions", 0);
    GET_BOOL(g_config.lock_surfaces, "lock_surfaces", FALSE);
    GET_BOOL(g_config.flipclear, "flipclear", FALSE);
    GET_BOOL(g_config.rgb555, "rgb555", FALSE);
    GET_BOOL(g_config.no_dinput_hook, "no_dinput_hook", FALSE);
    GET_BOOL(g_config.center_cursor_fix, "center_cursor_fix", FALSE);
    GET_STRING("fake_mode", "", g_config.fake_mode, sizeof(g_config.fake_mode));
    GET_BOOL(g_config.lock_mouse_top_left, "lock_mouse_top_left", FALSE);
    GET_STRING("win_version", "", g_config.win_version, sizeof(g_config.win_version));
    GET_INT(g_config.hook, "hook", 4);
    GET_BOOL(g_config.limit_gdi_handles, "limit_gdi_handles", FALSE);
    GET_BOOL(g_config.remove_menu, "remove_menu", FALSE);
    GET_INT(g_config.refresh_rate, "refresh_rate", 0);
    GET_BOOL(g_config.terminate_process, "terminate_process", FALSE);

    /* ddtest, our test program, stops its test when the window is closed
       instead of quitting, so it gets the close itself. Forced, because the
       generated directcrt.ini sets game_handles_close=false for everything. */
    GameHandlesClose = GameHandlesClose || _stricmp(g_config.process_file_name, "ddtest") == 0;

    if (g_config.lock_mouse_top_left)
        g_config.adjmouse = FALSE;

    ini_free(&g_config.ini);
}

/* remember_window: the window's size and position, if the user changed them */
void cfg_save()
{
    if (g_config.remember_window == REMEMBER_WINDOW_OFF)
        return;

    /* Do not save settings while macOS maximize is active */
    if (IsMacOS() && !g_config.window_rect.left && !g_config.window_rect.top)
        return;

    char buf[32];
    char* section =
        g_config.remember_window == REMEMBER_WINDOW_GLOBAL ? "directcrt" : g_config.process_file_name;

    if (g_config.window_rect.right > 0 && g_config.window_rect.bottom > 0)
    {
        ds_format_size_pixels(buf, sizeof(buf), g_config.window_rect.right, g_config.window_rect.bottom);
        WritePrivateProfileString(section, "window_size", buf, g_config.ini_path);
    }

    /* A window left of or above the primary monitor isn't saved: window_position can't be negative */
    if (g_config.window_rect.left != -32000 && g_config.window_rect.top != -32000 &&
        g_config.window_rect.left >= 0 && g_config.window_rect.top >= 0)
    {
        ds_format_position_pixels(buf, sizeof(buf), g_config.window_rect.left, g_config.window_rect.top);
        WritePrivateProfileString(section, "window_position", buf, g_config.ini_path);
    }

    /* Alt+Enter's state isn't saved: it lasts until the game exits, so every
       start uses 'display' */

    /* Our own change, not one to reload */
    cfg_ini_stat(&g_ini_seen);
}

static void cfg_ini_stat(WIN32_FILE_ATTRIBUTE_DATA* data)
{
    if (!GetFileAttributesExA(g_config.ini_path, GetFileExInfoStandard, data))
        memset(data, 0, sizeof(*data));
}

/* Live reload. Called every half second or so while the game runs: when
 * directcrt.ini has changed and then stayed the same for a call (an editor
 * may write it in several steps), or at once when 'now' is set (the settings
 * OSD wrote it), reads crt-auto's settings from it again into 'settings',
 * the other settings that apply while the game runs into g_config, and
 * returns TRUE. The rest need a restart */
BOOL cfg_reload_settings(CRT_AUTO_SETTINGS* settings, BOOL now)
{
    if (!g_config.ini_path[0])
        return FALSE;

    WIN32_FILE_ATTRIBUTE_DATA stat;
    cfg_ini_stat(&stat);

    if (!now)
    {
        if (CompareFileTime(&stat.ftLastWriteTime, &g_ini_seen.ftLastWriteTime) != 0 ||
            stat.nFileSizeLow != g_ini_seen.nFileSizeLow ||
            stat.nFileSizeHigh != g_ini_seen.nFileSizeHigh)
        {
            g_ini_seen = stat;
            g_ini_changed = TRUE;
            return FALSE;
        }

        /* Deleted, or being replaced */
        if (!g_ini_changed || !stat.nFileSizeLow)
            return FALSE;
    }

    g_ini_seen = stat;
    g_ini_changed = FALSE;

    TRACE("directcrt.ini changed, reading the settings that apply at once again\n");

    ini_create(&g_config.ini, g_config.ini_path);
    cfg_get_game_section(g_config.game_section, sizeof(g_config.game_section));
    cfg_load_crt_auto_settings(settings);
    cfg_load_live_settings();
    ini_free(&g_config.ini);

    return TRUE;
}

static void cfg_create_ini()
{
    FILE* fh = fopen(g_config.ini_path, "w");
    if (fh)
    {
        settings_write_ini(fh);
        fclose(fh);
    }
}

static void cfg_init()
{
    /* get process filename and directory */
    if (GetModuleFileNameA(NULL, g_config.game_path, sizeof(g_config.game_path) - 1) > 0)
    {
        _splitpath(g_config.game_path, NULL, NULL, g_config.process_file_name, g_config.process_file_ext);

        int len = strlen(g_config.game_path) - strlen(g_config.process_file_name) - strlen(g_config.process_file_ext);
        char* end = strstr(g_config.game_path + len, g_config.process_file_name);

        if (end)
        {
            *end = 0;
        }
        else
        {
            g_config.game_path[0] = 0;
        }
    }

    /* get dll filename and directory */
    if (GetModuleFileNameA(g_ddraw_module, g_config.dll_path, sizeof(g_config.dll_path) - 1) > 0)
    {
        _splitpath(g_config.dll_path, NULL, NULL, g_config.dll_file_name, g_config.dll_file_ext);

        int len = strlen(g_config.dll_path) - strlen(g_config.dll_file_name) - strlen(g_config.dll_file_ext);
        char* end = strstr(g_config.dll_path + len, g_config.dll_file_name);

        if (end)
        {
            *end = 0;
        }
        else
        {
            g_config.dll_path[0] = 0;
        }
    }

    if (!GetEnvironmentVariableA("DIRECTCRT_CONFIG_FILE", g_config.ini_path, sizeof(g_config.ini_path) - 1))
    {
        if (strlen(g_config.dll_path) > 0 && strlen(g_config.dll_file_name) > 0)
        {
            _snprintf(g_config.ini_path, sizeof(g_config.ini_path) - 1, "%sdirectcrt.ini", g_config.dll_path);

            /* Use this here instead to sync .ini filename with .dll filename - by egornovivan @ github */
            //_snprintf(g_config.ini_path, sizeof(g_config.ini_path) - 1, "%s%s.ini", g_config.dll_path, g_config.dll_file_name);

            if (GetFileAttributes(g_config.ini_path) == INVALID_FILE_ATTRIBUTES)
            {
                cfg_create_ini();
            }

            if (GetFileAttributes(g_config.ini_path) == INVALID_FILE_ATTRIBUTES)
            {
                /* This might not actually be needed, but we keep it for now */
                strncpy(g_config.ini_path, ".\\directcrt.ini", sizeof(g_config.ini_path) - 1);
            }
        }
        else
        {
            strncpy(g_config.ini_path, ".\\directcrt.ini", sizeof(g_config.ini_path) - 1);
        }
    }

    if (GetFileAttributes(g_config.ini_path) == INVALID_FILE_ATTRIBUTES)
    {
        cfg_create_ini();
    }

    ini_create(&g_config.ini, g_config.ini_path);
    cfg_get_game_section(g_config.game_section, sizeof(g_config.game_section));

    TRACE("filename = %s\n", g_config.process_file_name);
    TRACE("section  = %s\n", g_config.game_section[0] ? g_config.game_section : "directcrt");
}

/* The game's own section: [game] for game.exe */
static DWORD cfg_get_game_section(LPSTR buf, DWORD size)
{
    if (!buf || size == 0)
        return 0;

    if (ini_section_exists(&g_config.ini, g_config.process_file_name))
    {
        strncpy(buf, g_config.process_file_name, size);
        buf[size - 1] = 0;
        return strlen(buf);
    }

    buf[0] = 0;

    return 0;
}

static DWORD cfg_get_string(LPCSTR key, LPCSTR default_value, LPSTR buf, DWORD size)
{
    if (g_config.game_section[0])
    {
        DWORD s = ini_get_string(&g_config.ini, g_config.game_section, key, "", buf, size);

        if (s > 0)
            return s;
    }

    return ini_get_string(&g_config.ini, "directcrt", key, default_value, buf, size);
}

/* An old cnc-ddraw key's value in one section, translated to its new key's
 * value into buf; FALSE when the old key isn't set there */
typedef BOOL(*CFG_LEGACY)(LPCSTR section, LPSTR buf, DWORD size);

static BOOL cfg_copy(LPCSTR value, LPSTR buf, DWORD size)
{
    if (!value)
        return FALSE;

    strncpy(buf, value, size);
    buf[size - 1] = 0;
    return TRUE;
}

static DWORD cfg_get_raw(LPCSTR section, LPCSTR key, LPSTR buf, DWORD size)
{
    return ini_get_string(&g_config.ini, section, key, "", buf, size);
}

static BOOL cfg_legacy_display(LPCSTR section, LPSTR buf, DWORD size)
{
    char windowed[16], fullscreen[16];
    cfg_get_raw(section, "windowed", windowed, sizeof(windowed));
    cfg_get_raw(section, "fullscreen", fullscreen, sizeof(fullscreen));

    return cfg_copy(ds_legacy_display(windowed, fullscreen), buf, size);
}

static BOOL cfg_legacy_window_size(LPCSTR section, LPSTR buf, DWORD size)
{
    char width[16], height[16];
    cfg_get_raw(section, "width", width, sizeof(width));
    cfg_get_raw(section, "height", height, sizeof(height));

    return ds_legacy_window_size(width, height, buf, (int)size);
}

static BOOL cfg_legacy_window_position(LPCSTR section, LPSTR buf, DWORD size)
{
    char x[16], y[16];
    cfg_get_raw(section, "posX", x, sizeof(x));
    cfg_get_raw(section, "posY", y, sizeof(y));

    return ds_legacy_window_position(x, y, buf, (int)size);
}

static BOOL cfg_legacy_border(LPCSTR section, LPSTR buf, DWORD size)
{
    return cfg_get_raw(section, "border", buf, size) > 0;
}

static BOOL cfg_legacy_resizable(LPCSTR section, LPSTR buf, DWORD size)
{
    return cfg_get_raw(section, "resizable", buf, size) > 0;
}

static BOOL cfg_legacy_remember_window(LPCSTR section, LPSTR buf, DWORD size)
{
    char value[16];
    cfg_get_raw(section, "savesettings", value, sizeof(value));

    return cfg_copy(ds_legacy_remember_window(value), buf, size);
}

static BOOL cfg_legacy_integer_scaling(LPCSTR section, LPSTR buf, DWORD size)
{
    char value[16];
    cfg_get_raw(section, "boxing", value, sizeof(value));

    int boxing;
    if (!ds_parse_bool(value, &boxing) || !boxing)
        return FALSE;

    return cfg_copy("vertical", buf, size);
}

/* A setting that replaced old keys: in the game's section, the new key and
 * then the old ones, then the same in the global section */
static void cfg_get_new_or_old(
    LPCSTR key,
    LPCSTR default_value,
    CFG_LEGACY legacy,
    LPCSTR old_keys,
    LPSTR buf,
    DWORD size)
{
    LPCSTR sections[] = { g_config.game_section, "directcrt" };

    for (int i = 0; i < 2; i++)
    {
        if (!sections[i][0])
            continue;

        if (cfg_get_raw(sections[i], key, buf, size) > 0)
            return;

        if (legacy && legacy(sections[i], buf, size))
        {
            TRACE("     [%s] has the old setting %s, read as %s=%s\n", sections[i], old_keys, key, buf);
            return;
        }
    }

    cfg_copy(default_value, buf, size);
}

static BOOL cfg_parse_on_off(LPCSTR key, LPCSTR value)
{
    int on;

    if (!ds_parse_bool(value, &on))
    {
        TRACE("%s=%s is invalid, using on\n", key, value);
        on = 1;
    }

    TRACE("%s=%s\n", key, on ? "on" : "off");
    return on;
}

static void cfg_load_display_settings()
{
    char value[64];

    cfg_get_new_or_old("display", "auto", cfg_legacy_display, "windowed/fullscreen", value, sizeof(value));
    if (!ds_parse_display(value, &g_config.display))
    {
        TRACE("display=%s is invalid, using auto\n", value);
        g_config.display = DISPLAY_AUTO;
    }
    TRACE("display=%s\n", ds_display_name(g_config.display));

    cfg_get_new_or_old("window_size", "default", cfg_legacy_window_size, "width/height", value, sizeof(value));
    if (ds_parse_window_size(value, &g_config.window_size))
    {
        TRACE("window_size=%s\n", value);
    }
    else
    {
        TRACE("window_size=%s is invalid, using default\n", value);
        ds_parse_window_size("default", &g_config.window_size);
    }

    cfg_get_new_or_old("window_position", "auto", cfg_legacy_window_position, "posX/posY", value, sizeof(value));
    if (ds_parse_window_position(value, &g_config.window_position))
    {
        TRACE("window_position=%s\n", value);
    }
    else
    {
        TRACE("window_position=%s is invalid, using auto\n", value);
        ds_parse_window_position("auto", &g_config.window_position);
    }

    cfg_get_new_or_old("window_decorations", "on", cfg_legacy_border, "border", value, sizeof(value));
    g_config.window_decorations = cfg_parse_on_off("window_decorations", value);

    cfg_get_new_or_old("window_resizable", "on", cfg_legacy_resizable, "resizable", value, sizeof(value));
    g_config.window_resizable = cfg_parse_on_off("window_resizable", value);

    /* Until the game sets its cooperative level, as for a fullscreen game */
    DISPLAY_KIND kind = ds_display_start(g_config.display, 1);
    g_config.windowed = kind == DISPLAY_KIND_WINDOW;
    g_config.fullscreen = kind == DISPLAY_KIND_DESKTOP;
}

/* The settings that apply while the game runs, apart from crt-auto's
 * (SETTING_LIVE in settings.c) */
static void cfg_load_live_settings()
{
    char value[32];

    cfg_get_string("emulated_refresh", "auto", value, sizeof(value));
    g_config.emulated_refresh = _stricmp(value, "auto") == 0 ? 0 : strtoul(value, NULL, 10);

    if (g_config.emulated_refresh && (g_config.emulated_refresh < 24 || g_config.emulated_refresh > 240))
    {
        TRACE("     emulated_refresh=%s is invalid, using auto\n", value);
        g_config.emulated_refresh = 0;
    }

    TRACE("emulated_refresh=%lu\n", g_config.emulated_refresh);

    cfg_get_new_or_old("remember_window", "off", cfg_legacy_remember_window, "savesettings", value, sizeof(value));
    if (!ds_parse_remember_window(value, &g_config.remember_window))
    {
        TRACE("remember_window=%s is invalid, using off\n", value);
        g_config.remember_window = REMEMBER_WINDOW_OFF;
    }
    TRACE("remember_window=%s\n", ds_remember_window_name(g_config.remember_window));

    static const char* const osd_sizes[] = { "small", "medium", "large" };
    static const char* const osd_details[] = { "basic", "full" };
    static const char* const osd_positions[] = { "top-left", "top-right", "bottom-left", "bottom-right" };
    g_config.status_osd_size = cfg_get_choice("status_osd_size", osd_sizes, 3, 1);
    g_config.status_osd_full = cfg_get_choice("status_osd_detail", osd_details, 2, 0) == 1;
    g_config.status_osd_position = cfg_get_choice("status_osd_position", osd_positions, 4, 0);

    GET_INT(g_config.hotkeys.toggle_fullscreen, "keytogglefullscreen", VK_RETURN);
    GET_INT(g_config.hotkeys.toggle_fullscreen2, "keytogglefullscreen2", 0);
    GET_INT(g_config.hotkeys.toggle_maximize, "keytogglemaximize", VK_NEXT);
    GET_INT(g_config.hotkeys.toggle_maximize2, "keytogglemaximize2", 0);
    GET_INT(g_config.hotkeys.unlock_cursor1, "keyunlockcursor1", VK_TAB);
    GET_INT(g_config.hotkeys.unlock_cursor2, "keyunlockcursor2", VK_RCONTROL);
    GET_BOOL(g_config.hotkeys.unlock_cursor_middleclick, "unlockcursor_middleclick", FALSE);
    GET_INT(g_config.hotkeys.screenshot, "keyscreenshot", VK_SNAPSHOT);
    GET_INT(g_config.hotkeys.status_osd, "keystatusosd", VK_F11);
    GET_INT(g_config.hotkeys.settings_osd, "keysettingsosd", VK_F12);
}

/* The shader and crt-auto's settings use DOSBox Staging's names, values and
 * defaults. An invalid value gets the default, as in DOSBox */
static void cfg_load_crt_auto_settings(CRT_AUTO_SETTINGS* settings)
{
    *settings = crt_auto_default_settings();

    for (int i = 0; i < crt_auto_setting_count(); i++)
    {
        const char* name = crt_auto_setting_name(i);
        const char* default_value = crt_auto_setting_default(i);

        char value[MAX_PATH];
        cfg_get_new_or_old(
            name,
            default_value,
            strcmp(name, "integer_scaling") == 0 ? cfg_legacy_integer_scaling : NULL,
            "boxing",
            value,
            sizeof(value));

        /* cnc-ddraw's shader names and files */
        if (strcmp(name, "shader") == 0)
        {
            const char* shader = ds_legacy_shader(value);

            if (shader && _stricmp(shader, value) != 0)
            {
                TRACE("     shader=%s is no longer available, using %s\n", value, shader);
                cfg_copy(shader, value, sizeof(value));
            }
        }

        if (crt_auto_set(settings, name, value))
        {
            TRACE("%s=%s\n", name, value);
        }
        else
        {
            TRACE("%s=%s is invalid, using %s\n", name, value, default_value);
        }
    }
}

/* One of a list of words; an invalid value gets the default */
static int cfg_get_choice(LPCSTR key, const char* const* names, int count, int default_index)
{
    char value[64];
    cfg_get_string(key, names[default_index], value, sizeof(value));

    for (int i = 0; i < count; i++)
    {
        if (_stricmp(value, names[i]) == 0)
        {
            TRACE("%s=%s\n", key, names[i]);
            return i;
        }
    }

    TRACE("%s=%s is invalid, using %s\n", key, value, names[default_index]);
    return default_index;
}

static BOOL cfg_get_bool(LPCSTR key, BOOL default_value)
{
    char value[8];
    cfg_get_string(key, default_value ? "Yes" : "No", value, sizeof(value));

    int on;
    return ds_parse_bool(value, &on) ? on : default_value;
}

static int cfg_get_int(LPCSTR key, int default_value)
{
    char def_value[24];
    _snprintf(def_value, sizeof(def_value) - 1, "%d", default_value);

    char value[20];
    cfg_get_string(key, def_value, value, sizeof(value));

    if (strstr(value, "0x"))
    {
        return (int)strtol(value, NULL, 0);
    }
    else
    {
        return atoi(value);
    }
}
