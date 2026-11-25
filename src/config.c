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
#include "overlay.h"
#include "vbeam.h"
#include "versionhelpers.h"

static void cfg_init();
static void cfg_create_ini();
static LPCSTR cfg_default(LPCSTR key);
static BOOL cfg_get_bool(LPCSTR key);
static int cfg_get_int(LPCSTR key);
static DWORD cfg_get_string(LPCSTR key, LPCSTR default_value, LPSTR out_string, DWORD out_size);
static void cfg_load_crt_auto_settings(CRT_AUTO_SETTINGS* settings);
static void cfg_load_display_settings();
static void cfg_load_runtime_settings();
static void cfg_load_live_settings();
static void cfg_ini_stat(WIN32_FILE_ATTRIBUTE_DATA* data);
static int cfg_get_choice(LPCSTR key);

/* The defaults are settings.c's */
#define GET_INT(a,b) a = cfg_get_int(b); TRACE("%s=%d\n", b, a)
#define GET_BOOL(a,b) a = cfg_get_bool(b); TRACE("%s=%s\n", b, a ? "true" : "false")
#define GET_STRING(a,b,c) cfg_get_string(a, cfg_default(a), b, c); TRACE("%s=%s\n", a, b)

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

    /* Until the game sets its cooperative level, as for a fullscreen game */
    DISPLAY_KIND kind = ds_display_start(g_config.display, 1);
    g_config.windowed = kind == DISPLAY_KIND_WINDOW;
    g_config.fullscreen = kind == DISPLAY_KIND_DESKTOP;

    cfg_load_runtime_settings();
    cfg_load_crt_auto_settings(&g_config.crt);

    /* emulated_refresh, remember_window, the status OSD and the hotkeys */
    cfg_load_live_settings();
    overlay_set_status(g_config.status_osd);

    /* Compatibility settings */

    GET_BOOL(g_config.noactivateapp, "noactivateapp");
    GET_BOOL(g_config.nonexclusive, "nonexclusive");
    GET_BOOL(g_config.singlecpu, "singlecpu");
    GET_INT(g_config.resolutions, "resolutions");
    GET_INT(g_config.fixchilds, "fixchilds");
    GET_BOOL(g_config.hook_peekmessage, "hook_peekmessage");
    GET_INT(g_config.windowed_bpp, "windowed_bpp");

    /* Undocumented settings */

    GET_BOOL(g_config.fix_alt_key_stuck, "fix_alt_key_stuck");
    GET_BOOL(GameHandlesClose, "game_handles_close");
    GET_BOOL(g_config.fix_not_responding, "fix_not_responding");
    GET_BOOL(g_config.no_compat_warning, "no_compat_warning");
    GET_INT(g_config.guard_lines, "guard_lines");
    GET_INT(g_config.max_resolutions, "max_resolutions");
    GET_BOOL(g_config.lock_surfaces, "lock_surfaces");
    GET_BOOL(g_config.flipclear, "flipclear");
    GET_BOOL(g_config.rgb555, "rgb555");
    GET_BOOL(g_config.no_dinput_hook, "no_dinput_hook");
    GET_BOOL(g_config.center_cursor_fix, "center_cursor_fix");
    GET_STRING("fake_mode", g_config.fake_mode, sizeof(g_config.fake_mode));
    GET_BOOL(g_config.lock_mouse_top_left, "lock_mouse_top_left");
    GET_STRING("win_version", g_config.win_version, sizeof(g_config.win_version));
    GET_INT(g_config.hook, "hook");
    GET_BOOL(g_config.limit_gdi_handles, "limit_gdi_handles");
    GET_BOOL(g_config.remove_menu, "remove_menu");
    GET_INT(g_config.refresh_rate, "refresh_rate");
    GET_BOOL(g_config.terminate_process, "terminate_process");

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
    char* section = CFG_SECTION;

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

/* Alt+F11: the status OSD is shown or hidden the same way the next time the
 * game starts */
void cfg_save_status_osd(BOOL shown)
{
    g_config.status_osd = shown;

    if (!g_config.ini_path[0])
        return;

    WritePrivateProfileStringA(CFG_SECTION, "status_osd", shown ? "on" : "off", g_config.ini_path);

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
    cfg_load_display_settings();
    cfg_load_runtime_settings();
    cfg_load_crt_auto_settings(settings);
    cfg_load_live_settings();
    ini_free(&g_config.ini);

    if (g_config.lock_mouse_top_left)
        g_config.adjmouse = FALSE;

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

    TRACE("filename = %s\n", g_config.process_file_name);
}

/* Everything is in [directcrt]: the ini is the game's own, next to its
 * ddraw.dll. Sections named after a game, as cnc-ddraw had, aren't read */
static DWORD cfg_get_string(LPCSTR key, LPCSTR default_value, LPSTR buf, DWORD size)
{
    return ini_get_string(&g_config.ini, CFG_SECTION, key, default_value, buf, size);
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

/* A setting that replaced old keys: the new key, then the old ones, then the default */
static void cfg_get_new_or_old(
    LPCSTR key,
    CFG_LEGACY legacy,
    LPCSTR old_keys,
    LPSTR buf,
    DWORD size)
{
    if (cfg_get_raw(CFG_SECTION, key, buf, size) > 0)
        return;

    if (legacy && legacy(CFG_SECTION, buf, size))
    {
        TRACE("     the old setting %s, read as %s=%s\n", old_keys, key, buf);
        return;
    }

    cfg_copy(cfg_default(key), buf, size);
}

static BOOL cfg_parse_on_off(LPCSTR key, LPCSTR value)
{
    int on;

    if (!ds_parse_bool(value, &on))
    {
        TRACE("%s=%s is invalid, using %s\n", key, value, cfg_default(key));
        on = ds_parse_bool(cfg_default(key), &on) && on;
    }

    TRACE("%s=%s\n", key, on ? "on" : "off");
    return on;
}

static void cfg_load_display_settings()
{
    char value[64];

    cfg_get_new_or_old("display", cfg_legacy_display, "windowed/fullscreen", value, sizeof(value));
    if (!ds_parse_display(value, &g_config.display))
    {
        TRACE("display=%s is invalid, using %s\n", value, cfg_default("display"));
        ds_parse_display(cfg_default("display"), &g_config.display);
    }
    TRACE("display=%s\n", ds_display_name(g_config.display));

    cfg_get_new_or_old("window_size", cfg_legacy_window_size, "width/height", value, sizeof(value));
    if (ds_parse_window_size(value, &g_config.window_size))
    {
        TRACE("window_size=%s\n", value);
    }
    else
    {
        TRACE("window_size=%s is invalid, using %s\n", value, cfg_default("window_size"));
        ds_parse_window_size(cfg_default("window_size"), &g_config.window_size);
    }

    cfg_get_new_or_old("window_position", cfg_legacy_window_position, "posX/posY", value, sizeof(value));
    if (ds_parse_window_position(value, &g_config.window_position))
    {
        TRACE("window_position=%s\n", value);
    }
    else
    {
        TRACE("window_position=%s is invalid, using %s\n", value, cfg_default("window_position"));
        ds_parse_window_position(cfg_default("window_position"), &g_config.window_position);
    }

    cfg_get_new_or_old("window_decorations", cfg_legacy_border, "border", value, sizeof(value));
    g_config.window_decorations = cfg_parse_on_off("window_decorations", value);

    cfg_get_new_or_old("window_resizable", cfg_legacy_resizable, "resizable", value, sizeof(value));
    g_config.window_resizable = cfg_parse_on_off("window_resizable", value);

    cfg_get_string("fullscreen_minimize", cfg_default("fullscreen_minimize"), value, sizeof(value));
    g_config.fullscreen_minimize = cfg_parse_on_off("fullscreen_minimize", value);
}

/* maxfps to screenshotdir: they apply while the game runs too, the picture
 * set up again where they need it (dd_reload_settings) */
static void cfg_load_runtime_settings()
{
    GET_INT(g_config.maxfps, "maxfps");
    GET_BOOL(g_config.vsync, "vsync");
    GET_INT(g_config.maxgameticks, "maxgameticks");
    GET_INT(g_config.limiter_type, "limiter_type");
    GET_INT(g_config.minfps, "minfps");
    GET_BOOL(g_config.adjmouse, "adjmouse");
    GET_STRING("renderer", g_config.renderer, sizeof(g_config.renderer));
    const char* renderer = ds_legacy_renderer(g_config.renderer);
    if (renderer)
    {
        TRACE("     renderer=%s is no longer available, using %s\n", g_config.renderer, renderer);
        strncpy(g_config.renderer, renderer, sizeof(g_config.renderer) - 1);
    }
    GET_BOOL(g_config.devmode, "devmode");
    GET_INT(g_config.anti_aliased_fonts_min_size, "anti_aliased_fonts_min_size");
    GET_STRING("screenshotdir", g_config.screenshot_dir, sizeof(g_config.screenshot_dir));
}

/* The settings that apply while the game runs, apart from crt-auto's
 * (SETTING_LIVE in settings.c) */
static void cfg_load_live_settings()
{
    char value[32];

    cfg_get_string("emulated_refresh", cfg_default("emulated_refresh"), value, sizeof(value));
    g_config.emulated_refresh = _stricmp(value, "auto") == 0 ? 0 : strtoul(value, NULL, 10);

    if (g_config.emulated_refresh && (g_config.emulated_refresh < VBEAM_MIN_HZ || g_config.emulated_refresh > VBEAM_MAX_HZ))
    {
        TRACE("     emulated_refresh=%s is invalid, using auto\n", value);
        g_config.emulated_refresh = 0;
    }

    TRACE("emulated_refresh=%lu\n", g_config.emulated_refresh);

    cfg_get_new_or_old("remember_window", cfg_legacy_remember_window, "savesettings", value, sizeof(value));
    if (!ds_parse_remember_window(value, &g_config.remember_window))
    {
        TRACE("remember_window=%s is invalid, using %s\n", value, cfg_default("remember_window"));
        ds_parse_remember_window(cfg_default("remember_window"), &g_config.remember_window);
    }
    TRACE("remember_window=%s\n", ds_remember_window_name(g_config.remember_window));

    GET_BOOL(g_config.status_osd, "status_osd");

    /* By their places in settings.c's choices */
    g_config.status_osd_size = cfg_get_choice("status_osd_size");
    g_config.status_osd_full = cfg_get_choice("status_osd_detail") == 1;
    g_config.status_osd_position = cfg_get_choice("status_osd_position");
    GET_INT(g_config.status_osd_opacity, "status_osd_opacity");
    g_config.status_osd_opacity = max(0, min(100, g_config.status_osd_opacity));

    GET_INT(g_config.hotkeys.toggle_fullscreen, "keytogglefullscreen");
    GET_INT(g_config.hotkeys.toggle_fullscreen2, "keytogglefullscreen2");
    GET_INT(g_config.hotkeys.toggle_maximize, "keytogglemaximize");
    GET_INT(g_config.hotkeys.toggle_maximize2, "keytogglemaximize2");
    GET_INT(g_config.hotkeys.unlock_cursor1, "keyunlockcursor1");
    GET_INT(g_config.hotkeys.unlock_cursor2, "keyunlockcursor2");
    GET_BOOL(g_config.hotkeys.unlock_cursor_middleclick, "unlockcursor_middleclick");
    GET_INT(g_config.hotkeys.screenshot, "keyscreenshot");
    GET_INT(g_config.hotkeys.status_osd, "keystatusosd");
    GET_INT(g_config.hotkeys.settings_osd, "keysettingsosd");
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

/* A setting's default: its row in settings.c, or "" for one whose ini line
 * is commented out (it has an example value, not a default) */
static LPCSTR cfg_default(LPCSTR key)
{
    const SETTING* s = settings_find(key);

    if (!s || !s->value)
    {
        TRACE("     %s isn't in settings.c\n", key);
        return "";
    }

    return s->flags & SETTING_COMMENTED ? "" : s->value;
}

/* value's place in a setting's choices, -1 if it isn't one */
static int cfg_choice_index(const SETTING* s, LPCSTR value)
{
    char name[64], label[2];

    for (int i = 0; settings_choice(s, i, name, sizeof(name), label, sizeof(label)); i++)
    {
        if (_stricmp(value, name) == 0)
            return i;
    }

    return -1;
}

/* One of a SETTING_CHOICE's choices, by its place in them; an invalid value gets the default */
static int cfg_get_choice(LPCSTR key)
{
    const SETTING* s = settings_find(key);
    char value[64];
    cfg_get_string(key, cfg_default(key), value, sizeof(value));

    int i = cfg_choice_index(s, value);

    if (i >= 0)
    {
        TRACE("%s=%s\n", key, value);
        return i;
    }

    i = cfg_choice_index(s, cfg_default(key));
    TRACE("%s=%s is invalid, using %s\n", key, value, cfg_default(key));
    return i >= 0 ? i : 0;
}

static BOOL cfg_get_bool(LPCSTR key)
{
    char value[8];
    cfg_get_string(key, cfg_default(key), value, sizeof(value));

    int on;

    if (ds_parse_bool(value, &on))
        return on;

    return ds_parse_bool(cfg_default(key), &on) && on;
}

static int cfg_get_int(LPCSTR key)
{
    char value[20];
    cfg_get_string(key, cfg_default(key), value, sizeof(value));

    if (strstr(value, "0x"))
    {
        return (int)strtol(value, NULL, 0);
    }
    else
    {
        return atoi(value);
    }
}
