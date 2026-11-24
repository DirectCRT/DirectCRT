/* crt-auto for VGA, ported from DOSBox Staging (GPL-2.0-or-later):
 *
 *   - shader choice: src/gui/render/auto_shader_switcher.cpp,
 *     AutoShaderSwitcher::NotifyRenderParametersChanged and GetVgaShader
 *   - picture size: src/gui/render/render.cpp,
 *     RENDER_CalcRestrictedViewportSizeInPixels and RENDER_CalcDrawRectInPixels
 *   - the picture's shape: src/hardware/video/vga_draw.cpp, the pixel aspect
 *     ratio for each 'aspect' mode
 *   - double scanning and pixel doubling: the S3 VESA mode table in
 *     src/ints/int10_modes.cpp
 *   - settings: render.cpp (the [render] section) and window_geometry.cpp
 *   - colour: src/gui/render/auto_image_adjustments.cpp and the setting
 *     remaps in render.cpp
 *
 * DOSBox does the geometry in float (DosBox::Rect) and rounds with roundf;
 * this does the same so the results match to the pixel.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include "crt_auto.h"


static int iroundf(float x)
{
    return (int)roundf(x);
}

/* DOSBox's VGA BIOS draws its low-resolution VESA modes on a standard timing
 * by doubling: 320x200, 320x240, 400x300 and 512x384 are both pixel doubled
 * and double scanned (to 640x400, 640x480, 800x600, 1024x768); 320x400 and
 * 320x480 are pixel doubled only. Mode 13h and Mode X are both. */
static int is_pixel_doubled(int mode_width)
{
    return mode_width <= 512;
}

static int is_double_scanned(int mode_height)
{
    return mode_height <= 384;
}

/* A fraction, kept as DOSBox's Fraction class keeps it (reduced, in 64 bits),
 * so it turns into the same float */
typedef struct FRACTION
{
    long long num;
    long long den;
} FRACTION;

static long long gcd64(long long a, long long b)
{
    if (a < 0)
        a = -a;

    if (b < 0)
        b = -b;

    while (b)
    {
        long long t = a % b;
        a = b;
        b = t;
    }

    return a ? a : 1;
}

static FRACTION fraction(long long num, long long den)
{
    long long g = gcd64(num, den);
    FRACTION f = { num / g, den / g };
    return f;
}

static float fraction_float(FRACTION f)
{
    return (float)f.num / (float)f.den;
}

/* The shape of the picture on the screen. A VGA monitor shows every mode at
 * 4:3. Modes wider than 16:10 didn't exist on VGA; they're for widescreen
 * displays, so they keep square pixels (DOSBox does the same for its custom
 * VESA resolutions) */
static FRACTION display_aspect(CRT_ASPECT aspect, int mode_width, int mode_height, int viewport_w, int viewport_h)
{
    switch (aspect)
    {
    case CRT_ASPECT_SQUARE_PIXELS:
        return fraction(mode_width, mode_height);

    case CRT_ASPECT_STRETCH:
        /* The viewport's shape, in whole pixels */
        if (viewport_w > 0 && viewport_h > 0)
            return fraction(viewport_w, viewport_h);

        return fraction(mode_width, mode_height);

    default:
        /* The low resolution VGA modes (up to 720 pixels wide, below 480
         * lines and no wider than 16:10: 320x200, 320x400, 640x400, 720x400)
         * have non-square pixels and are shown at 4:3, as on a CRT. Every
         * other mode has square pixels (1:1). DOSBox shows 1280x1024 at 4:3
         * too; DirectCRT keeps its square pixels, as on the 5:4 monitors it
         * was made for */
        if (mode_width <= 720 && mode_height < 480 && mode_width * 10 <= mode_height * 16)
            return fraction(4, 3);

        return fraction(mode_width, mode_height);
    }
}

/* The pixel aspect ratio of a render_width x render_height image shown at
 * that shape */
static FRACTION pixel_aspect(FRACTION display, int render_width, int render_height)
{
    return fraction(display.num * render_height, display.den * render_width);
}

typedef struct RECTF
{
    float x;
    float y;
    float w;
    float h;
} RECTF;

/* Rect::ScaleSizeToFit */
static RECTF scale_size_to_fit(RECTF r, float dest_w, float dest_h)
{
    if (r.w <= 0 || r.h <= 0 || dest_w <= 0 || dest_h <= 0)
        return r;

    float sw = dest_w / r.w;
    float sh = dest_h / r.h;
    float s = sw < sh ? sw : sh;

    r.w *= s;
    r.h *= s;

    return r;
}

/* The desktop in logical units, as SDL gives it to DOSBox (not rounded) */
static float desktop_logical(int px, float scale)
{
    return (float)px / scale;
}

/* parse_fit_viewport_modes rejects a size larger than the desktop, compared
 * in whole units as DOSBox shows them */
static int viewport_in_bounds(const CRT_VIEWPORT* v, const CRT_AUTO_DISPLAY* d)
{
    float bw = desktop_logical(d->desktop_width, d->scale);
    float bh = desktop_logical(d->desktop_height, d->scale);

    if (v->mode == CRT_VIEWPORT_LIMIT_PX)
    {
        /* GFX_LogicalToPixels */
        bw *= d->scale;
        bh *= d->scale;
    }
    else if (v->mode != CRT_VIEWPORT_LIMIT)
    {
        return 1;
    }

    return iroundf(v->width) <= iroundf(bw) && iroundf(v->height) <= iroundf(bh);
}

/* RENDER_CalcRestrictedViewportSizeInPixels */
static RECTF restricted_viewport(const CRT_VIEWPORT* v, const CRT_AUTO_DISPLAY* d, int canvas_w, int canvas_h)
{
    RECTF canvas = { 0, 0, (float)canvas_w, (float)canvas_h };
    RECTF vp = canvas;

    switch (v->mode)
    {
    case CRT_VIEWPORT_RELATIVE:
    {
        /* A 4:3 box fitted to the canvas, then scaled; it can be larger
         * than the canvas */
        RECTF box = { 0, 0, 4, 3 };
        box = scale_size_to_fit(box, canvas.w, canvas.h);
        box.w *= v->width;
        box.h *= v->height;
        return box;
    }

    case CRT_VIEWPORT_LIMIT:
        /* GFX_LogicalToPixels */
        vp.w = v->width * d->scale;
        vp.h = v->height * d->scale;
        break;

    case CRT_VIEWPORT_LIMIT_PX:
        vp.w = v->width;
        vp.h = v->height;
        break;

    case CRT_VIEWPORT_DESKTOP:
        /* GFX_LogicalToPixels(GFX_GetDesktopSize()).ScaleSize(scale) */
        vp.w = desktop_logical(d->desktop_width, d->scale) * d->scale * v->width;
        vp.h = desktop_logical(d->desktop_height, d->scale) * d->scale * v->height;
        break;

    default:
        return canvas;
    }

    /* Within the canvas (Rect::Intersect of two rectangles at 0,0) */
    if (vp.w > canvas.w)
        vp.w = canvas.w;

    if (vp.h > canvas.h)
        vp.h = canvas.h;

    return vp;
}

/* RENDER_CalcDrawRectInPixels, before rounding. Unlike DOSBox, whose 'auto'
 * snaps only the shaders under 'crt/' (ShaderDescriptor::
 * EnforceAutoIntegerScaling), 'auto' snaps every shader: sharp, nearest and
 * bilinear too */
static RECTF calc_draw_rect(
    CRT_INTEGER_SCALING integer_scaling,
    RECTF viewport,
    int canvas_w,
    int canvas_h,
    int render_w,
    int render_h,
    FRACTION par)
{
    float rw = (float)render_w;
    float rh = (float)render_h;
    float par_f = fraction_float(par);

    RECTF fit = { 0, 0, rw * par_f, rh };
    fit = scale_size_to_fit(fit, viewport.w, viewport.h);

    RECTF r = fit;

    switch (integer_scaling)
    {
    case CRT_INTEGER_SCALING_HORIZONTAL:
    {
        int factor = iroundf(fit.w) / iroundf(rw);

        if (factor >= 1)
        {
            FRACTION inverse = { par.den, par.num };

            r.w = rw * (float)factor;
            r.h = rh * (float)factor * fraction_float(inverse);
        }
        break;
    }

    case CRT_INTEGER_SCALING_VERTICAL:
    case CRT_INTEGER_SCALING_AUTO:
    {
        float factor = fit.h / rh;

        if (integer_scaling == CRT_INTEGER_SCALING_VERTICAL)
        {
            factor = floorf(factor);
        }
        else if (factor >= 5.0f)
        {
            /* No snapping from 5x */
        }
        else if (factor >= 3.0f)
        {
            /* Half steps from 3x to 5x */
            factor = floorf(factor * 2) / 2;
        }
        else
        {
            /* Whole steps below 3x */
            factor = floorf(factor);
        }

        /* Below 1x: fit */
        if (factor >= 1.0f)
        {
            r.w = rw * factor * par_f;
            r.h = rh * factor;
        }
        break;
    }

    default:
        break;
    }

    /* CenterTo the canvas, not the viewport */
    r.x = (float)canvas_w / 2 - r.w / 2;
    r.y = (float)canvas_h / 2 - r.h / 2;

    return r;
}

CRT_AUTO_LAYOUT crt_auto_calc_layout(
    const CRT_AUTO_SETTINGS* settings,
    const CRT_AUTO_DISPLAY* display,
    int mode_width,
    int mode_height,
    int canvas_width,
    int canvas_height)
{
    CRT_AUTO_LAYOUT layout;
    memset(&layout, 0, sizeof(layout));

    if (mode_width <= 0 || mode_height <= 0 || canvas_width <= 0 || canvas_height <= 0)
    {
        layout.render_width = mode_width > 0 ? mode_width : 1;
        layout.render_height = mode_height > 0 ? mode_height : 1;
        layout.width = canvas_width > 0 ? canvas_width : 0;
        layout.height = canvas_height > 0 ? canvas_height : 0;
        return layout;
    }

    CRT_AUTO_SETTINGS defaults = crt_auto_default_settings();
    const CRT_AUTO_SETTINGS* s = settings ? settings : &defaults;

    CRT_AUTO_DISPLAY d = { canvas_width, canvas_height, 1.0f };
    if (display && display->desktop_width > 0 && display->desktop_height > 0 && display->scale > 0)
        d = *display;

    CRT_VIEWPORT vp_setting = s->viewport;
    if (!viewport_in_bounds(&vp_setting, &d))
    {
        layout.viewport_rejected = 1;
        vp_setting.mode = CRT_VIEWPORT_FIT;
    }

    RECTF viewport = restricted_viewport(&vp_setting, &d, canvas_width, canvas_height);

    FRACTION aspect =
        display_aspect(s->aspect, mode_width, mode_height, iroundf(viewport.w), iroundf(viewport.h));

    layout.double_scanned = is_double_scanned(mode_height);
    layout.pixel_doubled = is_pixel_doubled(mode_width);

    /* AutoShaderSwitcher: pixels per scanline from the video mode with
     * double scanning applied to both dimensions.
     *
     * DOSBox measures the draw rectangle it would get with the shader that's
     * active at the time. With 'integer_scaling = auto', whole and half steps
     * don't change the whole part of the scale factor, so measuring with them
     * gives the same result as DOSBox measuring without them (sharp) */
    {
        int ds = layout.double_scanned ? 2 : 1;
        int rw = mode_width * ds;
        int rh = mode_height * ds;

        RECTF r = calc_draw_rect(
            s->integer_scaling,
            viewport,
            canvas_width,
            canvas_height,
            rw,
            rh,
            pixel_aspect(aspect, rw, rh));

        layout.pixels_per_scanline = iroundf(r.h) / rh;
    }

    /* A fixed shader, or GetVgaShader */
    if (s->shader == CRT_SHADER_SETTING_SHARP)
    {
        layout.shader = CRT_SHADER_SHARP;
    }
    else if (s->shader == CRT_SHADER_SETTING_NEAREST)
    {
        layout.shader = CRT_SHADER_NEAREST;
    }
    else if (s->shader == CRT_SHADER_SETTING_BILINEAR)
    {
        layout.shader = CRT_SHADER_BILINEAR;
    }
    else if (layout.pixels_per_scanline >= 4)
    {
        layout.shader = CRT_SHADER_HYLLIAN_VGA_4K;
    }
    else if (layout.pixels_per_scanline >= 3)
    {
        layout.shader = CRT_SHADER_HYLLIAN_VGA_1440P;
    }
    else if (layout.pixels_per_scanline >= 2)
    {
        /* Up to 1080/5 = 216 lines: shown single scanned at 5x, with the
         * scanlines faked */
        if (layout.double_scanned && mode_height <= 1080 / 5)
            layout.shader = CRT_SHADER_VGA_1080P_FAKE_DOUBLE_SCAN;
        else
            layout.shader = CRT_SHADER_VGA_1080P;
    }
    else
    {
        layout.shader = CRT_SHADER_SHARP;
    }

    /* The image the shader gets: sharp and nearest force single scanning and
     * no pixel doubling, the fake double scan shader forces single scanning.
     * bilinear has neither pragma, so it gets the doubled image */
    {
        int single = layout.shader == CRT_SHADER_SHARP || layout.shader == CRT_SHADER_NEAREST;
        int pd = layout.pixel_doubled && !single ? 2 : 1;
        int ds =
            layout.double_scanned &&
            !single &&
            layout.shader != CRT_SHADER_VGA_1080P_FAKE_DOUBLE_SCAN ? 2 : 1;

        layout.render_width = mode_width * pd;
        layout.render_height = mode_height * ds;
    }

    /* RENDER_CalcDrawRectInPixels, rounded as in GFX_CalcDrawRectInPixels.
     * With 'viewport = relative' the picture can be larger than the canvas */
    {
        RECTF r = calc_draw_rect(
            s->integer_scaling,
            viewport,
            canvas_width,
            canvas_height,
            layout.render_width,
            layout.render_height,
            pixel_aspect(aspect, layout.render_width, layout.render_height));

        layout.x = iroundf(r.x);
        layout.y = iroundf(r.y);
        layout.width = iroundf(r.w);
        layout.height = iroundf(r.h);
    }

    return layout;
}

const char* crt_auto_shader_name(CRT_SHADER shader)
{
    switch (shader)
    {
    case CRT_SHADER_SHARP: return "interpolation/sharp";
    case CRT_SHADER_VGA_1080P: return "crt/vga-1080p";
    case CRT_SHADER_VGA_1080P_FAKE_DOUBLE_SCAN: return "crt/vga-1080p-fake-double-scan";
    case CRT_SHADER_HYLLIAN_VGA_1440P: return "crt/crt-hyllian:vga-1440p";
    case CRT_SHADER_HYLLIAN_VGA_4K: return "crt/crt-hyllian:vga-4k";
    case CRT_SHADER_NEAREST: return "interpolation/nearest";
    case CRT_SHADER_BILINEAR: return "interpolation/bilinear";
    default: return "?";
    }
}


void crt_auto_params_set(CRT_AUTO_PARAMS* params, const char* name, float value)
{
    for (int i = 0; i < params->count; i++)
    {
        if (strcmp(params->param[i].name, name) == 0)
        {
            params->param[i].value = value;
            return;
        }
    }

    size_t len = strlen(name);

    if (params->count < CRT_AUTO_MAX_PARAMS && len < sizeof(params->param[0].name))
    {
        memcpy(params->param[params->count].name, name, len + 1);
        params->param[params->count].value = value;
        params->count++;
    }
}

float crt_auto_params_get(const CRT_AUTO_PARAMS* params, const char* name, float fallback)
{
    for (int i = 0; i < params->count; i++)
    {
        if (strcmp(params->param[i].name, name) == 0)
            return params->param[i].value;
    }

    return fallback;
}

static const char* skip_spaces(const char* p)
{
    while (*p == ' ' || *p == '\t')
        p++;

    return p;
}

/* Copies an identifier; returns the position after it, or NULL */
static const char* read_name(const char* p, char* name, size_t size)
{
    size_t n = 0;

    while (isalnum((unsigned char)*p) || *p == '_')
    {
        if (n + 1 >= size)
            return NULL;

        name[n++] = *p++;
    }

    name[n] = '\0';

    return n > 0 ? p : NULL;
}

void crt_auto_params_from_shader(CRT_AUTO_PARAMS* params, const char* source)
{
    static const char tag[] = "#pragma parameter";

    for (const char* p = strstr(source, tag); p; p = strstr(p, tag))
    {
        p += sizeof(tag) - 1;

        char name[32];
        const char* q = read_name(skip_spaces(p), name, sizeof(name));
        if (!q)
            continue;

        /* Skip the quoted label */
        q = skip_spaces(q);
        if (*q != '"')
            continue;

        q = strchr(q + 1, '"');
        if (!q)
            break;

        char* end;
        float value = strtof(q + 1, &end);
        if (end == q + 1)
            continue;

        crt_auto_params_set(params, name, value);
        p = end;
    }
}

int crt_auto_params_apply_preset(CRT_AUTO_PARAMS* params, const char* preset)
{
    int set = 0;

    for (const char* line = preset; line && *line; )
    {
        const char* next = strchr(line, '\n');

        char name[32];
        const char* q = read_name(skip_spaces(line), name, sizeof(name));

        if (q)
        {
            q = skip_spaces(q);

            if (*q == '=')
            {
                char* end;
                float value = strtof(q + 1, &end);

                if (end != q + 1)
                {
                    crt_auto_params_set(params, name, value);
                    set++;
                }
            }
        }

        line = next ? next + 1 : NULL;
    }

    return set;
}


/* Settings, read as DOSBox Staging reads its [render] section */

static int str_ieq(const char* a, const char* b)
{
    for (; *a && *b; a++, b++)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    }

    return *a == *b;
}

static int starts_with_i(const char* s, const char* prefix)
{
    for (; *prefix; s++, prefix++)
    {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
    }

    return 1;
}

/* parse_bool_setting, plus 1 and 0 as for DOSBox's bool settings */
static int parse_bool(const char* v, int* out)
{
    static const char* const on[] = { "enabled", "true", "on", "yes", "1" };
    static const char* const off[] = { "disabled", "false", "off", "no", "none", "0" };

    for (size_t i = 0; i < sizeof(on) / sizeof(on[0]); i++)
    {
        if (str_ieq(v, on[i]))
        {
            *out = 1;
            return 1;
        }
    }

    for (size_t i = 0; i < sizeof(off) / sizeof(off[0]); i++)
    {
        if (str_ieq(v, off[i]))
        {
            *out = 0;
            return 1;
        }
    }

    return 0;
}

/* has_true and has_false */
static int is_true(const char* v)
{
    int b;
    return parse_bool(v, &b) && b;
}

static int is_false(const char* v)
{
    int b;
    return parse_bool(v, &b) && !b;
}

/* parse_int: the whole text is the number */
static int parse_int(const char* v, int* out)
{
    char* end;

    if (!*v || isspace((unsigned char)*v))
        return 0;

    long n = strtol(v, &end, 10);
    if (*end || n < -1000000 || n > 1000000)
        return 0;

    *out = (int)n;
    return 1;
}

static int parse_float(const char* v, size_t len, float* out)
{
    char buf[32];
    char* end;

    if (len == 0 || len >= sizeof(buf) || isspace((unsigned char)*v))
        return 0;

    memcpy(buf, v, len);
    buf[len] = '\0';

    *out = strtof(buf, &end);
    return *end == '\0' && isfinite(*out);
}

/* parse_percentage_with_percent_sign: "N%" */
static int parse_percent(const char* v, size_t len, float* out)
{
    return len >= 2 && v[len - 1] == '%' && parse_float(v, len - 1, out);
}

static int parse_int_in_range(const char* v, int min, int max, int* out)
{
    int n;

    if (!parse_int(v, &n) || n < min || n > max)
        return 0;

    *out = n;
    return 1;
}

/* WindowGeometry::ParseWindowSizeSetting's WxH and WxHpx forms (the viewport
 * setting doesn't take its percentage and named forms): two whole numbers
 * from 1 to 65535 separated by 'x' or ',' */
static int parse_size(const char* v, CRT_VIEWPORT* out)
{
    size_t len = strlen(v);
    int px = 0;

    if (len > 2 && tolower((unsigned char)v[len - 2]) == 'p' && tolower((unsigned char)v[len - 1]) == 'x')
    {
        px = 1;
        len -= 2;
    }

    char buf[32];
    if (len == 0 || len >= sizeof(buf))
        return 0;

    memcpy(buf, v, len);
    buf[len] = '\0';

    char* sep = NULL;
    for (char* p = buf; *p; p++)
    {
        if (*p == 'x' || *p == 'X' || *p == ',')
        {
            if (sep)
                return 0;

            sep = p;
        }
    }

    if (!sep)
        return 0;

    *sep = '\0';

    int w, h;
    if (!parse_int(buf, &w) || !parse_int(sep + 1, &h) || w <= 0 || h <= 0 || w > 65535 || h > 65535)
        return 0;

    out->mode = px ? CRT_VIEWPORT_LIMIT_PX : CRT_VIEWPORT_LIMIT;
    out->width = (float)w;
    out->height = (float)h;
    return 1;
}

/* parse_viewport_settings. The check against the desktop size is done in
 * crt_auto_calc_layout, where the desktop is known */
static int parse_viewport(const char* v, CRT_VIEWPORT* out)
{
    CRT_VIEWPORT vp = { CRT_VIEWPORT_FIT, 0, 0 };

    if (starts_with_i(v, "relative"))
    {
        /* relative H% V%, separated by spaces */
        const char* p = v + 8;
        const char* parts[2] = { NULL, NULL };
        size_t lens[2] = { 0, 0 };
        int n = 0;

        if (*p != ' ' && *p != '\t')
            return 0;

        while (*p)
        {
            while (*p == ' ' || *p == '\t')
                p++;

            if (!*p)
                break;

            if (n == 2)
                return 0;

            parts[n] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;

            lens[n] = (size_t)(p - parts[n]);
            n++;
        }

        float w, h;
        if (n != 2 || !parse_percent(parts[0], lens[0], &w) || !parse_percent(parts[1], lens[1], &h))
            return 0;

        w /= 100.0f;
        h /= 100.0f;

        if (w < 0.2f || w > 3.0f || h < 0.2f || h > 3.0f)
            return 0;

        vp.mode = CRT_VIEWPORT_RELATIVE;
        vp.width = w;
        vp.height = h;
    }
    else if (str_ieq(v, "fit"))
    {
        vp.mode = CRT_VIEWPORT_FIT;
    }
    else if (!parse_size(v, &vp))
    {
        float p;
        if (!parse_percent(v, strlen(v), &p) || p < 1.0f || p > 100.0f)
            return 0;

        vp.mode = CRT_VIEWPORT_DESKTOP;
        vp.width = p / 100.0f;
        vp.height = p / 100.0f;
    }

    *out = vp;
    return 1;
}

static const char* const g_setting_names[] = {
    "shader",
    "aspect",
    "integer_scaling",
    "viewport",
    "image_adjustments",
    "crt_color_profile",
    "brightness",
    "contrast",
    "gamma",
    "digital_contrast",
    "black_level",
    "saturation",
    "color_temperature",
    "color_temperature_luma_preserve",
    "red_gain",
    "green_gain",
    "blue_gain",
};

static const char* const g_setting_defaults[] = {
    "crt-auto", /* shader */
    "auto", /* aspect */
    "auto", /* integer_scaling */
    "fit",  /* viewport */
    "on",   /* image_adjustments */
    "auto", /* crt_color_profile */
    "45",   /* brightness */
    "65",   /* contrast */
    "0",    /* gamma */
    "0",    /* digital_contrast */
    "auto", /* black_level */
    "0",    /* saturation */
    "auto", /* color_temperature */
    "0",    /* color_temperature_luma_preserve */
    "100",  /* red_gain */
    "100",  /* green_gain */
    "100",  /* blue_gain */
};

int crt_auto_setting_count(void)
{
    return (int)(sizeof(g_setting_names) / sizeof(g_setting_names[0]));
}

const char* crt_auto_setting_name(int i)
{
    return i >= 0 && i < crt_auto_setting_count() ? g_setting_names[i] : NULL;
}

const char* crt_auto_setting_default(int i)
{
    return i >= 0 && i < crt_auto_setting_count() ? g_setting_defaults[i] : NULL;
}

/* Returns 0 for an invalid value, and changes nothing then */
static int parse_setting(CRT_AUTO_SETTINGS* s, const char* name, const char* v)
{
    if (str_ieq(name, "shader"))
    {
        if (str_ieq(v, "crt-auto"))
            s->shader = CRT_SHADER_SETTING_CRT_AUTO;
        else if (str_ieq(v, "sharp"))
            s->shader = CRT_SHADER_SETTING_SHARP;
        else if (str_ieq(v, "nearest"))
            s->shader = CRT_SHADER_SETTING_NEAREST;
        else if (str_ieq(v, "bilinear"))
            s->shader = CRT_SHADER_SETTING_BILINEAR;
        else
            return 0;

        return 1;
    }

    if (str_ieq(name, "aspect"))
    {
        if (is_true(v) || str_ieq(v, "auto"))
            s->aspect = CRT_ASPECT_AUTO;
        else if (is_false(v) || str_ieq(v, "square-pixels"))
            s->aspect = CRT_ASPECT_SQUARE_PIXELS;
        else if (str_ieq(v, "stretch"))
            s->aspect = CRT_ASPECT_STRETCH;
        else
            return 0;

        return 1;
    }

    if (str_ieq(name, "integer_scaling"))
    {
        if (is_false(v))
            s->integer_scaling = CRT_INTEGER_SCALING_OFF;
        else if (str_ieq(v, "auto"))
            s->integer_scaling = CRT_INTEGER_SCALING_AUTO;
        else if (str_ieq(v, "vertical"))
            s->integer_scaling = CRT_INTEGER_SCALING_VERTICAL;
        else if (str_ieq(v, "horizontal"))
            s->integer_scaling = CRT_INTEGER_SCALING_HORIZONTAL;
        else
            return 0;

        return 1;
    }

    if (str_ieq(name, "viewport"))
        return parse_viewport(v, &s->viewport);

    if (str_ieq(name, "image_adjustments"))
        return parse_bool(v, &s->image_adjustments);

    if (str_ieq(name, "crt_color_profile"))
    {
        /* In the shader's order: 0 none, 1 EBU, 2 P22, ... */
        static const char* const profiles[] = { "none", "ebu", "p22", "smpte-c", "philips", "trinitron" };

        if (str_ieq(v, "auto"))
        {
            s->crt_color_profile = CRT_AUTO_SETTING_AUTO;
            return 1;
        }

        if (is_false(v))
        {
            s->crt_color_profile = 0;
            return 1;
        }

        for (int i = 0; i < (int)(sizeof(profiles) / sizeof(profiles[0])); i++)
        {
            if (str_ieq(v, profiles[i]))
            {
                s->crt_color_profile = i;
                return 1;
            }
        }

        return 0;
    }

    if (str_ieq(name, "black_level"))
    {
        if (str_ieq(v, "auto"))
        {
            s->black_level = CRT_AUTO_SETTING_AUTO;
            return 1;
        }

        return parse_int_in_range(v, 0, 100, &s->black_level);
    }

    if (str_ieq(name, "color_temperature"))
    {
        if (str_ieq(v, "auto"))
        {
            s->color_temperature = CRT_AUTO_SETTING_AUTO;
            return 1;
        }

        return parse_int_in_range(v, 3000, 10000, &s->color_temperature);
    }

    static const struct
    {
        const char* name;
        size_t offset;
        int min;
        int max;
    } ints[] = {
        { "brightness", offsetof(CRT_AUTO_SETTINGS, brightness), 0, 100 },
        { "contrast", offsetof(CRT_AUTO_SETTINGS, contrast), 0, 100 },
        { "gamma", offsetof(CRT_AUTO_SETTINGS, gamma), -50, 50 },
        { "digital_contrast", offsetof(CRT_AUTO_SETTINGS, digital_contrast), -50, 50 },
        { "saturation", offsetof(CRT_AUTO_SETTINGS, saturation), -50, 50 },
        { "color_temperature_luma_preserve", offsetof(CRT_AUTO_SETTINGS, color_temperature_luma_preserve), 0, 100 },
        { "red_gain", offsetof(CRT_AUTO_SETTINGS, red_gain), 0, 200 },
        { "green_gain", offsetof(CRT_AUTO_SETTINGS, green_gain), 0, 200 },
        { "blue_gain", offsetof(CRT_AUTO_SETTINGS, blue_gain), 0, 200 },
    };

    for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); i++)
    {
        if (str_ieq(name, ints[i].name))
            return parse_int_in_range(v, ints[i].min, ints[i].max, (int*)((char*)s + ints[i].offset));
    }

    return 0;
}

CRT_AUTO_SETTINGS crt_auto_default_settings(void)
{
    CRT_AUTO_SETTINGS s;
    memset(&s, 0, sizeof(s));

    for (int i = 0; i < crt_auto_setting_count(); i++)
        parse_setting(&s, g_setting_names[i], g_setting_defaults[i]);

    return s;
}

int crt_auto_set(CRT_AUTO_SETTINGS* settings, const char* name, const char* value)
{
    int i;

    for (i = 0; i < crt_auto_setting_count(); i++)
    {
        if (str_ieq(name, g_setting_names[i]))
            break;
    }

    if (i == crt_auto_setting_count())
        return 0;

    /* Leading and trailing spaces don't count */
    while (*value == ' ' || *value == '\t')
        value++;

    size_t len = strlen(value);
    while (len && (value[len - 1] == ' ' || value[len - 1] == '\t' || value[len - 1] == '\r' || value[len - 1] == '\n'))
        len--;

    char buf[64];
    if (len < sizeof(buf))
    {
        memcpy(buf, value, len);
        buf[len] = '\0';

        if (parse_setting(settings, g_setting_names[i], buf))
            return 1;
    }

    parse_setting(settings, g_setting_names[i], g_setting_defaults[i]);
    return 0;
}


/* remap() in DOSBox's math_utils.h: invlerp, then std::lerp */
static float remap(float in_min, float in_max, float out_min, float out_max, float v)
{
    float t = (v - in_min) / (in_max - in_min);

    if ((out_min <= 0 && out_max >= 0) || (out_min >= 0 && out_max <= 0))
        return t * out_max + (1 - t) * out_min;

    if (t == 1)
        return out_max;

    float x = out_min + t * (out_max - out_min);

    if ((t > 1) == (out_max > out_min))
        return out_max < x ? x : out_max;

    return x < out_max ? x : out_max;
}

CRT_AUTO_COLOR crt_auto_vga_color(const CRT_AUTO_SETTINGS* settings)
{
    CRT_AUTO_SETTINGS defaults = crt_auto_default_settings();
    const CRT_AUTO_SETTINGS* s = settings ? settings : &defaults;
    CRT_AUTO_COLOR c;

    c.color_space = 0; /* color_space = srgb */
    c.enable_adjustments = s->image_adjustments ? 1 : 0;

    /* 'auto' for VGA (auto_image_adjustments.cpp): P22 phosphors */
    c.color_profile = s->crt_color_profile == CRT_AUTO_SETTING_AUTO ? 2 : s->crt_color_profile;

    c.brightness = remap(0, 100, 0, 100, (float)s->brightness);
    c.contrast = remap(0, 100, 0, 100, (float)s->contrast);
    c.gamma = remap(-50, 50, -1, 1, (float)s->gamma);
    c.digital_contrast = remap(-50, 50, -2, 2, (float)s->digital_contrast);

    /* VGA_GetBlackLevelColor: neutral dark grey */
    c.black_level_color[0] = 40 / 255.0f;
    c.black_level_color[1] = 40 / 255.0f;
    c.black_level_color[2] = 40 / 255.0f;

    /* 'auto' for VGA: no black level boost (0, which halving for sRGB's 2.2
     * gamma leaves at 0) */
    c.black_level = s->black_level == CRT_AUTO_SETTING_AUTO ? 0.0f : remap(0, 100, 0, 1, (float)s->black_level);

    c.saturation = remap(-50, 50, -1, 1, (float)s->saturation);

    /* 'auto' for VGA: 7800 K */
    c.color_temperature_kelvin =
        s->color_temperature == CRT_AUTO_SETTING_AUTO ? 7800.0f : (float)s->color_temperature;

    c.color_temperature_luma_preserve = remap(0, 100, 0, 1, (float)s->color_temperature_luma_preserve);
    c.red_gain = remap(0, 200, 0, 2, (float)s->red_gain);
    c.green_gain = remap(0, 200, 0, 2, (float)s->green_gain);
    c.blue_gain = remap(0, 200, 0, 2, (float)s->blue_gain);

    return c;
}
