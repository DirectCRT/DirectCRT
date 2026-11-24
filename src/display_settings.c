#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display_settings.h"

/* Parsed values are copied into this, lowercased and trimmed */
#define DS_BUF 128


static int iroundf_ds(float f)
{
    return (int)floorf(f + 0.5f);
}

static int copy_lower(char* dst, const char* src)
{
    if (!src)
        return 0;

    while (*src && isspace((unsigned char)*src))
        src++;

    size_t len = strlen(src);

    while (len > 0 && isspace((unsigned char)src[len - 1]))
        len--;

    if (len == 0 || len >= DS_BUF)
        return 0;

    for (size_t i = 0; i < len; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);

    dst[len] = 0;
    return 1;
}

static int is_set(const char* s)
{
    if (!s)
        return 0;

    while (*s && isspace((unsigned char)*s))
        s++;

    return *s != 0;
}

static int ieq(const char* a, const char* b)
{
    char buf[DS_BUF];
    return copy_lower(buf, a) && strcmp(buf, b) == 0;
}

int ds_parse_bool(const char* value, int* out)
{
    char s[DS_BUF];

    if (!copy_lower(s, value))
        return 0;

    if (!strcmp(s, "on") || !strcmp(s, "true") || !strcmp(s, "yes") || !strcmp(s, "1"))
    {
        *out = 1;
        return 1;
    }

    if (!strcmp(s, "off") || !strcmp(s, "false") || !strcmp(s, "no") || !strcmp(s, "0"))
    {
        *out = 0;
        return 1;
    }

    return 0;
}

int ds_parse_display(const char* value, DISPLAY_MODE* out)
{
    static const struct { const char* name; DISPLAY_MODE mode; } names[] = {
        { "auto", DISPLAY_AUTO },
        { "window", DISPLAY_WINDOW },
        { "fullscreen", DISPLAY_FULLSCREEN },
        { "original", DISPLAY_ORIGINAL },
    };

    char s[DS_BUF];

    if (!copy_lower(s, value))
        return 0;

    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
    {
        if (!strcmp(s, names[i].name))
        {
            *out = names[i].mode;
            return 1;
        }
    }

    return 0;
}

const char* ds_display_name(DISPLAY_MODE mode)
{
    switch (mode)
    {
    case DISPLAY_AUTO: return "auto";
    case DISPLAY_WINDOW: return "window";
    case DISPLAY_FULLSCREEN: return "fullscreen";
    case DISPLAY_ORIGINAL: return "original";
    default: return "?";
    }
}

int ds_parse_remember_window(const char* value, REMEMBER_WINDOW* out)
{
    char s[DS_BUF];

    if (!copy_lower(s, value))
        return 0;

    int on;

    /* global and game: before there was only [directcrt] */
    if (!strcmp(s, "global") || !strcmp(s, "game"))
        *out = REMEMBER_WINDOW_ON;
    else if (ds_parse_bool(s, &on))
        *out = on ? REMEMBER_WINDOW_ON : REMEMBER_WINDOW_OFF;
    else
        return 0;

    return 1;
}

const char* ds_remember_window_name(REMEMBER_WINDOW value)
{
    switch (value)
    {
    case REMEMBER_WINDOW_OFF: return "off";
    case REMEMBER_WINDOW_ON: return "on";
    default: return "?";
    }
}

/* A whole decimal integer, no sign */
static int parse_uint(const char* s, long* out)
{
    if (!*s)
        return 0;

    for (const char* p = s; *p; p++)
    {
        if (!isdigit((unsigned char)*p))
            return 0;
    }

    errno = 0;
    long v = strtol(s, NULL, 10);

    if (errno)
        return 0;

    *out = v;
    return 1;
}

static int parse_number(const char* s, GEOMETRY_UNIT unit, float* out)
{
    if (unit == GEOMETRY_PERCENT)
    {
        if (!*s)
            return 0;

        /* digits with at most one decimal point, no sign or exponent */
        int dots = 0, digits = 0;

        for (const char* p = s; *p; p++)
        {
            if (*p == '.')
                dots++;
            else if (isdigit((unsigned char)*p))
                digits++;
            else
                return 0;
        }

        if (dots > 1 || digits == 0)
            return 0;

        float v = (float)atof(s);

        if (v < 0.0f || v > 1000.0f)
            return 0;

        *out = v;
        return 1;
    }

    /* Large enough for any display, small enough to avoid overflows */
    long v;

    if (!parse_uint(s, &v) || v > 65535)
        return 0;

    *out = (float)v;
    return 1;
}

/* "<a><delim><b>[px|%]" with delim one of delimiters */
static int parse_pair(const char* value, const char* delimiters, float* a, float* b, GEOMETRY_UNIT* unit)
{
    char s[DS_BUF];

    if (!copy_lower(s, value))
        return 0;

    size_t len = strlen(s);
    GEOMETRY_UNIT u = GEOMETRY_LOGICAL;

    if (len >= 2 && !strcmp(s + len - 2, "px"))
    {
        s[len - 2] = 0;
        u = GEOMETRY_PIXELS;
    }
    else if (len >= 1 && s[len - 1] == '%')
    {
        s[len - 1] = 0;
        u = GEOMETRY_PERCENT;
    }

    char* sep = NULL;
    int seps = 0;

    for (char* p = s; *p; p++)
    {
        if (strchr(delimiters, *p))
        {
            sep = p;
            seps++;
        }
    }

    if (seps != 1)
        return 0;

    *sep = 0;

    float fa, fb;

    if (!parse_number(s, u, &fa) || !parse_number(sep + 1, u, &fb))
        return 0;

    *a = fa;
    *b = fb;
    *unit = u;
    return 1;
}

int ds_parse_window_size(const char* value, WINDOW_SIZE* out)
{
    char s[DS_BUF];

    if (!copy_lower(s, value))
        return 0;

    float height_percent = 0.0f;

    if (!strcmp(s, "s") || !strcmp(s, "small"))
        height_percent = 50.0f;
    else if (!strcmp(s, "m") || !strcmp(s, "medium") || !strcmp(s, "default"))
        height_percent = 74.0f;
    else if (!strcmp(s, "l") || !strcmp(s, "large"))
        height_percent = 90.0f;

    if (height_percent > 0.0f)
    {
        out->unscaled = 0;
        out->w = height_percent * 4.0f / 3.0f;
        out->h = height_percent;
        out->unit = GEOMETRY_PERCENT;
        return 1;
    }

    if (!strcmp(s, "unscaled"))
    {
        out->unscaled = 1;
        out->w = 0.0f;
        out->h = 0.0f;
        out->unit = GEOMETRY_PIXELS;
        return 1;
    }

    float w, h;
    GEOMETRY_UNIT unit;

    if (!parse_pair(s, "x,", &w, &h, &unit) || w <= 0.0f || h <= 0.0f)
        return 0;

    out->unscaled = 0;
    out->w = w;
    out->h = h;
    out->unit = unit;
    return 1;
}

int ds_parse_window_position(const char* value, WINDOW_POSITION* out)
{
    char s[DS_BUF];

    if (!copy_lower(s, value))
        return 0;

    if (!strcmp(s, "auto"))
    {
        out->is_auto = 1;
        out->x = 0.0f;
        out->y = 0.0f;
        out->unit = GEOMETRY_LOGICAL;
        return 1;
    }

    float x, y;
    GEOMETRY_UNIT unit;

    if (!parse_pair(s, ",", &x, &y, &unit) || x < 0.0f || y < 0.0f)
        return 0;

    out->is_auto = 0;
    out->x = x;
    out->y = y;
    out->unit = unit;
    return 1;
}

DISPLAY_KIND ds_display_start(DISPLAY_MODE mode, int game_fullscreen)
{
    switch (mode)
    {
    case DISPLAY_WINDOW:
        return DISPLAY_KIND_WINDOW;

    case DISPLAY_FULLSCREEN:
        return DISPLAY_KIND_DESKTOP;

    case DISPLAY_ORIGINAL:
        return game_fullscreen ? DISPLAY_KIND_MODE_SWITCH : DISPLAY_KIND_WINDOW;

    case DISPLAY_AUTO:
    default:
        return game_fullscreen ? DISPLAY_KIND_DESKTOP : DISPLAY_KIND_WINDOW;
    }
}

DISPLAY_KIND ds_display_fullscreen_kind(DISPLAY_MODE mode)
{
    return mode == DISPLAY_ORIGINAL ? DISPLAY_KIND_MODE_SWITCH : DISPLAY_KIND_DESKTOP;
}

void ds_window_client_size(
    const WINDOW_SIZE* size,
    int desktop_width,
    int desktop_height,
    float scale,
    int game_width,
    int game_height,
    int* out_width,
    int* out_height)
{
    (void)desktop_width;

    if (scale <= 0.0f)
        scale = 1.0f;

    if (size->unscaled)
    {
        *out_width = game_width;
        *out_height = game_height;
        return;
    }

    /* As DOSBox: the desktop in logical units isn't rounded */
    float desktop_h = (float)desktop_height / scale;
    float w, h;

    switch (size->unit)
    {
    case GEOMETRY_PIXELS:
        w = size->w / scale;
        h = size->h / scale;
        break;

    case GEOMETRY_PERCENT:
        w = size->w * desktop_h / 100.0f;
        h = size->h * desktop_h / 100.0f;
        break;

    case GEOMETRY_LOGICAL:
    default:
        w = size->w;
        h = size->h;
        break;
    }

    if (w < (float)WINDOW_MIN_WIDTH)
        w = (float)WINDOW_MIN_WIDTH;

    if (h < (float)WINDOW_MIN_HEIGHT)
        h = (float)WINDOW_MIN_HEIGHT;

    *out_width = iroundf_ds(w * scale);
    *out_height = iroundf_ds(h * scale);
}

void ds_window_position_pixels(
    const WINDOW_POSITION* position,
    int desktop_width,
    int desktop_height,
    float scale,
    int* out_x,
    int* out_y)
{
    if (scale <= 0.0f)
        scale = 1.0f;

    switch (position->unit)
    {
    case GEOMETRY_PIXELS:
        *out_x = iroundf_ds(position->x);
        *out_y = iroundf_ds(position->y);
        break;

    case GEOMETRY_PERCENT:
        *out_x = iroundf_ds(position->x * (float)desktop_width / 100.0f);
        *out_y = iroundf_ds(position->y * (float)desktop_height / 100.0f);
        break;

    case GEOMETRY_LOGICAL:
    default:
        *out_x = iroundf_ds(position->x * scale);
        *out_y = iroundf_ds(position->y * scale);
        break;
    }
}

void ds_format_size_pixels(char* buf, int buf_size, int width, int height)
{
    snprintf(buf, (size_t)buf_size, "%dx%dpx", width, height);
}

void ds_format_position_pixels(char* buf, int buf_size, int x, int y)
{
    snprintf(buf, (size_t)buf_size, "%d,%dpx", x, y);
}

/* cnc-ddraw's GetPrivateProfileInt-style values: whole numbers, possibly
 * negative; "true"/"false" for the bools */
static int legacy_bool(const char* s, int* out)
{
    char b[DS_BUF];

    if (!copy_lower(b, s))
        return 0;

    if (!strcmp(b, "true") || !strcmp(b, "yes") || !strcmp(b, "on"))
    {
        *out = 1;
        return 1;
    }

    if (!strcmp(b, "false") || !strcmp(b, "no") || !strcmp(b, "off"))
    {
        *out = 0;
        return 1;
    }

    char* end;
    long v = strtol(b, &end, 10);

    if (end == b)
        return 0;

    *out = v != 0;
    return 1;
}

static int legacy_int(const char* s, long* out)
{
    char b[DS_BUF];

    if (!copy_lower(b, s))
        return 0;

    char* end;
    long v = strtol(b, &end, 10);

    if (end == b || *end)
        return 0;

    *out = v;
    return 1;
}

const char* ds_legacy_display(const char* windowed, const char* fullscreen)
{
    if (!is_set(windowed))
        return NULL;

    if (ieq(windowed, "auto"))
        return "auto";

    int w;

    if (!legacy_bool(windowed, &w))
        return NULL;

    if (!w)
        return "fullscreen";

    /* windowed=true with fullscreen=true was borderless fullscreen */
    int f = 0;

    if (is_set(fullscreen) && legacy_bool(fullscreen, &f) && f)
        return "fullscreen";

    return "window";
}

int ds_legacy_window_size(const char* width, const char* height, char* buf, int buf_size)
{
    long w, h;

    if (!legacy_int(width, &w) || !legacy_int(height, &h))
        return 0;

    if (w <= 0 || h <= 0 || w > 65535 || h > 65535)
        return 0;

    ds_format_size_pixels(buf, buf_size, (int)w, (int)h);
    return 1;
}

int ds_legacy_window_position(const char* pos_x, const char* pos_y, char* buf, int buf_size)
{
    long x, y;

    if (!legacy_int(pos_x, &x) || !legacy_int(pos_y, &y))
        return 0;

    if (x == -32000 || y == -32000)
    {
        snprintf(buf, (size_t)buf_size, "auto");
        return 1;
    }

    /* window_position can't be negative (a window on a monitor left of the
     * primary one); those go back to the centre */
    if (x < 0 || y < 0 || x > 65535 || y > 65535)
    {
        snprintf(buf, (size_t)buf_size, "auto");
        return 1;
    }

    ds_format_position_pixels(buf, buf_size, (int)x, (int)y);
    return 1;
}

const char* ds_legacy_remember_window(const char* savesettings)
{
    long v;

    if (!legacy_int(savesettings, &v))
        return NULL;

    switch (v)
    {
    case 0: return "off";
    case 1: return "on";
    case 2: return "on";
    default: return NULL;
    }
}

const char* ds_legacy_shader(const char* shader)
{
    char s[DS_BUF];

    if (!copy_lower(s, shader))
        return NULL;

    static const char* const names[] = { "crt-auto", "sharp", "nearest", "bilinear" };

    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
    {
        if (!strcmp(s, names[i]))
            return names[i];
    }

    /* cnc-ddraw's name; its "Bilinear" is matched above */
    if (!strcmp(s, "nearest neighbor"))
        return "nearest";

    return "crt-auto";
}

const char* ds_legacy_renderer(const char* renderer)
{
    char s[DS_BUF];

    if (!copy_lower(s, renderer))
        return NULL;

    if (!strcmp(s, "openglcore") || !strcmp(s, "direct3d9") || !strcmp(s, "direct3d9on12") ||
        !strcmp(s, "gdi") || !strcmp(s, "software"))
        return "auto";

    return NULL;
}
