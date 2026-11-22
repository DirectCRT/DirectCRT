/* crt-auto for VGA, ported from DOSBox Staging (GPL-2.0-or-later):
 *
 *   - shader choice: src/gui/render/auto_shader_switcher.cpp,
 *     AutoShaderSwitcher::NotifyRenderParametersChanged and GetVgaShader
 *   - picture size: src/gui/render/render.cpp, RENDER_CalcDrawRectInPixels
 *     with 'viewport = fit', 'integer_scaling = auto'
 *   - double scanning and pixel doubling: the S3 VESA mode table in
 *     src/ints/int10_modes.cpp
 *   - colour: src/gui/render/auto_image_adjustments.cpp and the setting
 *     defaults in render.cpp
 *
 * DOSBox does the geometry in float (DosBox::Rect) and rounds with roundf;
 * this does the same so the results match to the pixel.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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

/* A VGA monitor shows every mode at 4:3. Modes wider than 16:10 didn't exist
 * on VGA; they're for widescreen displays, so they keep square pixels (DOSBox
 * does the same for its custom VESA resolutions). */
static float display_aspect(int mode_width, int mode_height)
{
    if (mode_width * 10 > mode_height * 16)
        return (float)mode_width / (float)mode_height;

    return 4.0f / 3.0f;
}

/* RENDER_CalcDrawRectInPixels, 'viewport = fit': the render size with its
 * pixel aspect ratio, scaled to fit the canvas. Returns the height; the width
 * is height * aspect */
static float fit_height(float render_width, float render_height, float pixel_aspect, int canvas_width, int canvas_height)
{
    float w = render_width * pixel_aspect;
    float h = render_height;

    float sw = (float)canvas_width / w;
    float sh = (float)canvas_height / h;
    float s = sw < sh ? sw : sh;

    return h * s;
}

static int is_crt_shader(CRT_SHADER shader)
{
    /* ShaderDescriptor::EnforceAutoIntegerScaling: the shaders under 'crt/'.
     * sharp is 'interpolation/sharp' */
    return shader != CRT_SHADER_SHARP;
}

CRT_AUTO_LAYOUT crt_auto_calc_layout(int mode_width, int mode_height, int canvas_width, int canvas_height)
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

    layout.double_scanned = is_double_scanned(mode_height);
    layout.pixel_doubled = is_pixel_doubled(mode_width);

    float aspect = display_aspect(mode_width, mode_height);

    /* AutoShaderSwitcher: pixels per scanline from the video mode with
     * double scanning applied to both dimensions */
    {
        int ds = layout.double_scanned ? 2 : 1;
        float rw = (float)(mode_width * ds);
        float rh = (float)(mode_height * ds);
        float par = aspect / (rw / rh);

        /* DOSBox measures the draw rectangle it would get with the shader
         * that's active at the time; whole steps don't change the whole part
         * of the scale factor, so the unsnapped fit gives the same result */
        int draw_h = iroundf(fit_height(rw, rh, par, canvas_width, canvas_height));

        layout.pixels_per_scanline = draw_h / iroundf(rh);
    }

    /* GetVgaShader */
    if (layout.pixels_per_scanline >= 4)
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

    /* The image the shader gets: sharp forces single scanning and no pixel
     * doubling, the fake double scan shader forces single scanning */
    {
        int pd = layout.pixel_doubled && layout.shader != CRT_SHADER_SHARP ? 2 : 1;
        int ds =
            layout.double_scanned &&
            layout.shader != CRT_SHADER_SHARP &&
            layout.shader != CRT_SHADER_VGA_1080P_FAKE_DOUBLE_SCAN ? 2 : 1;

        layout.render_width = mode_width * pd;
        layout.render_height = mode_height * ds;
    }

    /* RENDER_CalcDrawRectInPixels */
    {
        float rw = (float)layout.render_width;
        float rh = (float)layout.render_height;
        float par = aspect / (rw / rh);

        float h = fit_height(rw, rh, par, canvas_width, canvas_height);
        float w = rw * par * (h / rh);

        if (is_crt_shader(layout.shader))
        {
            /* 'integer_scaling = auto': whole steps below 3x, half steps
             * from 3x to 5x, no snapping from 5x */
            float factor = h / rh;
            float scale;

            if (factor >= 5.0f)
                scale = factor;
            else if (factor >= 3.0f)
                scale = floorf(factor * 2) / 2;
            else
                scale = floorf(factor);

            if (scale >= 1.0f)
            {
                h = rh * scale;
                w = rw * scale * par;
            }
        }

        /* CenterTo, then rounded as in GFX_CalcDrawRectInPixels */
        float x = (float)canvas_width / 2 - w / 2;
        float y = (float)canvas_height / 2 - h / 2;

        layout.x = iroundf(x);
        layout.y = iroundf(y);
        layout.width = iroundf(w);
        layout.height = iroundf(h);
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


CRT_AUTO_COLOR crt_auto_vga_color(void)
{
    CRT_AUTO_COLOR c;

    c.color_space = 0;                     /* color_space = srgb */
    c.enable_adjustments = 1;              /* image_adjustments = on */
    c.color_profile = 2;                   /* crt_color_profile = auto: P22 for VGA */
    c.brightness = 45.0f;                  /* brightness = 45 */
    c.contrast = 65.0f;                    /* contrast = 65 */
    c.gamma = 0.0f;                        /* gamma = 0 */
    c.digital_contrast = 0.0f;             /* digital_contrast = 0 */
    c.black_level_color[0] = 40 / 255.0f;  /* VGA_GetBlackLevelColor: neutral dark grey */
    c.black_level_color[1] = 40 / 255.0f;
    c.black_level_color[2] = 40 / 255.0f;
    c.black_level = 0.0f;                  /* black_level = auto: 0 for VGA */
    c.saturation = 0.0f;                   /* saturation = 0 */
    c.color_temperature_kelvin = 7800.0f;  /* color_temperature = auto: 7800 K for VGA */
    c.color_temperature_luma_preserve = 0; /* color_temperature_luma_preserve = 0 */
    c.red_gain = 1.0f;                     /* red_gain = 100 */
    c.green_gain = 1.0f;
    c.blue_gain = 1.0f;

    return c;
}
