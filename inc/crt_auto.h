#ifndef CRT_AUTO_H
#define CRT_AUTO_H

#include <stddef.h>

/* crt-auto for VGA, as in DOSBox Staging: which CRT shader to use, the size
 * and position of the picture, and the shader settings.
 *
 * Ported from DOSBox Staging (GPL-2.0-or-later): auto_shader_switcher.cpp
 * (shader choice), render.cpp RENDER_CalcDrawRectInPixels (picture size) and
 * auto_image_adjustments.cpp (colour). No Windows or OpenGL code, so it can be
 * unit tested on its own (tests/test_crt_auto.c).
 */

typedef enum CRT_SHADER
{
    CRT_SHADER_SHARP = 0,                  /* interpolation/sharp, under 2 pixels per line */
    CRT_SHADER_VGA_1080P,                  /* crt/vga-1080p */
    CRT_SHADER_VGA_1080P_FAKE_DOUBLE_SCAN, /* crt/vga-1080p-fake-double-scan */
    CRT_SHADER_HYLLIAN_VGA_1440P,          /* crt/crt-hyllian with the vga-1440p preset */
    CRT_SHADER_HYLLIAN_VGA_4K,             /* crt/crt-hyllian with the vga-4k preset */
    CRT_SHADER_NEAREST,                    /* interpolation/nearest, with shader = nearest */
    CRT_SHADER_BILINEAR,                   /* interpolation/bilinear, with shader = bilinear */
    CRT_SHADER_COUNT
} CRT_SHADER;

typedef struct CRT_AUTO_LAYOUT
{
    CRT_SHADER shader;

    /* How a VGA-class monitor showed the mode: lines drawn twice (320x200
     * came out as 400 lines) and pixels drawn twice (as 640 dots) */
    int double_scanned;
    int pixel_doubled;

    /* Whole screen pixels per game line, counting double-scanned lines
     * twice; this picks the shader */
    int pixels_per_scanline;

    /* The image the main shader reads: the game's picture with the doubling
     * that shader expects (crt-hyllian gets 640x400 for 320x200, sharp gets
     * 320x200) */
    int render_width;
    int render_height;

    /* The picture inside the canvas (the window's client area), top-left
     * origin. Everything outside it is black */
    int x;
    int y;
    int width;
    int height;

    /* The area the viewport setting gives, which the picture is fitted in:
     * centred as the picture is and moved with it, top-left origin. It can
     * be larger than the canvas */
    int viewport_x;
    int viewport_y;
    int viewport_width;
    int viewport_height;

    /* The viewport setting was larger than the desktop: DOSBox rejects it and
     * uses 'fit', and so does this */
    int viewport_rejected;
} CRT_AUTO_LAYOUT;

/* The DOSBox Staging [render] settings crt-auto uses, with DOSBox's names
 * and values (see crt_auto_set) */

typedef enum CRT_ASPECT
{
    CRT_ASPECT_AUTO = 0,      /* aspect = auto: 4:3, like a VGA monitor */
    CRT_ASPECT_SQUARE_PIXELS, /* aspect = square-pixels */
    CRT_ASPECT_STRETCH        /* aspect = stretch: fill the viewport */
} CRT_ASPECT;

typedef enum CRT_INTEGER_SCALING
{
    CRT_INTEGER_SCALING_AUTO = 0, /* whole and half steps, for the CRT shaders only */
    CRT_INTEGER_SCALING_VERTICAL,
    CRT_INTEGER_SCALING_HORIZONTAL,
    CRT_INTEGER_SCALING_OFF
} CRT_INTEGER_SCALING;

/* shader = crt-auto picks a shader from the pixels per line; the others
 * are fixed */
typedef enum CRT_SHADER_SETTING
{
    CRT_SHADER_SETTING_CRT_AUTO = 0,
    CRT_SHADER_SETTING_SHARP,
    CRT_SHADER_SETTING_NEAREST,
    CRT_SHADER_SETTING_BILINEAR,
    CRT_SHADER_SETTING_COUNT
} CRT_SHADER_SETTING;

typedef enum CRT_VIEWPORT_MODE
{
    CRT_VIEWPORT_FIT = 0,      /* viewport = fit: the whole canvas */
    CRT_VIEWPORT_LIMIT,        /* viewport = WxH: at most this size, in logical units */
    CRT_VIEWPORT_LIMIT_PX,     /* viewport = WxHpx: at most this size, in pixels */
    CRT_VIEWPORT_RELATIVE,     /* viewport = relative H% V%: H% and V% of the desktop's size */
    CRT_VIEWPORT_COUNT
} CRT_VIEWPORT_MODE;

typedef struct CRT_VIEWPORT
{
    CRT_VIEWPORT_MODE mode;

    /* LIMIT and LIMIT_PX: the size. RELATIVE: the fractions of the desktop's
     * width and height (0.1 to 2) */
    float width;
    float height;
} CRT_VIEWPORT;

/* The viewport setting's value; 0, and out unchanged, if it isn't valid */
int crt_auto_parse_viewport(const char* v, CRT_VIEWPORT* out);

#define CRT_AUTO_OFFSET_MAX 0.5f

/* The viewport_offset setting's value, "H% V%", as fractions of the picture's
 * width and height; 0, and x and y unchanged, if it isn't valid */
int crt_auto_parse_offset(const char* v, float* x, float* y);

#define CRT_AUTO_SETTING_AUTO (-1000)

typedef struct CRT_AUTO_SETTINGS
{
    CRT_SHADER_SETTING shader;
    CRT_ASPECT aspect;
    CRT_INTEGER_SCALING integer_scaling;
    CRT_VIEWPORT viewport;
    int color_space; /* 0 srgb, 1 dci-p3, 2 dci-p3-d65, 3 display-p3, 4 modern-p3, 5 adobe-rgb, 6 rec-2020 */

    /* viewport_offset, DirectCRT's own: the picture moved from where the
     * viewport puts it, by fractions of its width and height; right and down */
    float offset_x;
    float offset_y;

    /* Image adjustments, in the settings' own ranges. CRT_AUTO_SETTING_AUTO
     * where 'auto' is allowed */
    int image_adjustments;               /* on/off */
    int crt_color_profile;               /* auto, or 0 none, 1 EBU, 2 P22, 3 SMPTE-C, 4 Philips, 5 Trinitron */
    int brightness;                      /* 0 to 100 */
    int contrast;                        /* 0 to 100 */
    int gamma;                           /* -50 to 50 */
    int digital_contrast;                /* -50 to 50 */
    int black_level;                     /* auto, or 0 to 100 */
    int saturation;                      /* -50 to 50 */
    int color_temperature;               /* auto, or 3000 to 10000 K */
    int color_temperature_luma_preserve; /* 0 to 100 */
    int red_gain;                        /* 0 to 200 */
    int green_gain;
    int blue_gain;
} CRT_AUTO_SETTINGS;

/* DOSBox Staging's defaults */
CRT_AUTO_SETTINGS crt_auto_default_settings(void);

/* The settings' names, and their defaults as text, for i = 0 to
 * crt_auto_setting_count() - 1 */
int crt_auto_setting_count(void);
const char* crt_auto_setting_name(int i);
const char* crt_auto_setting_default(int i);
int crt_auto_setting_range(int i, int* min, int* max); /* 0 if it isn't a number */

/* Sets a setting from its text, as DOSBox Staging reads it. Returns 1 when
 * the value is valid; otherwise sets the default and returns 0 (DOSBox shows
 * a warning and uses the default). Also returns 0 for a name that isn't a
 * crt-auto setting, and changes nothing */
int crt_auto_set(CRT_AUTO_SETTINGS* settings, const char* name, const char* value);

/* The display the canvas is on: the desktop size in pixels and the display
 * scale (DPI / 96), for viewport sizes in logical units or relative to the
 * desktop */
typedef struct CRT_AUTO_DISPLAY
{
    int desktop_width;
    int desktop_height;
    float scale;
} CRT_AUTO_DISPLAY;

/* mode: the game's resolution. canvas: the area the picture goes in.
 * settings and display can be NULL: DOSBox's defaults, and a desktop the size
 * of the canvas at 100% */
CRT_AUTO_LAYOUT crt_auto_calc_layout(
    const CRT_AUTO_SETTINGS* settings,
    const CRT_AUTO_DISPLAY* display,
    int mode_width,
    int mode_height,
    int canvas_width,
    int canvas_height);

/* A shader: DOSBox Staging's name for it, and the resources (DirectCRT.rc)
 * its GLSL source and preset are in */
typedef struct CRT_SHADER_INFO
{
    const char* name;
    const char* resource;
    const char* preset; /* NULL: none */
} CRT_SHADER_INFO;

/* An unknown shader is interpolation/sharp */
const CRT_SHADER_INFO* crt_auto_shader_info(CRT_SHADER shader);
const char* crt_auto_shader_name(CRT_SHADER shader);

/* The shader setting's value: crt-auto, sharp, nearest or bilinear */
const char* crt_auto_shader_setting_name(CRT_SHADER_SETTING shader);


#define CRT_AUTO_MAX_PARAMS 32

typedef struct CRT_AUTO_PARAM
{
    char name[32];
    float value;
} CRT_AUTO_PARAM;

typedef struct CRT_AUTO_PARAMS
{
    int count;
    CRT_AUTO_PARAM param[CRT_AUTO_MAX_PARAMS];
} CRT_AUTO_PARAMS;

/* Adds the defaults from a shader's '#pragma parameter NAME "label" default
 * min max step' lines */
void crt_auto_params_from_shader(CRT_AUTO_PARAMS* params, const char* source);

/* Applies a preset's 'NAME = value' lines on top. Returns the number of
 * values set */
int crt_auto_params_apply_preset(CRT_AUTO_PARAMS* params, const char* preset);

/* Sets one value, adding it if it isn't there yet */
void crt_auto_params_set(CRT_AUTO_PARAMS* params, const char* name, float value);

/* Returns the value, or 'fallback' when there's no such parameter */
float crt_auto_params_get(const CRT_AUTO_PARAMS* params, const char* name, float fallback);


/* The values for DOSBox Staging's image-adjustments shader. The shader's own
 * uniform names are in the comments */
typedef struct CRT_AUTO_COLOR
{
    int color_space;                       /* COLOR_SPACE: as CRT_AUTO_SETTINGS' */
    int enable_adjustments;                /* ENABLE_ADJUSTMENTS */
    int color_profile;                     /* COLOR_PROFILE: 0 none, 1 EBU, 2 P22, 3 SMPTE-C, 4 Philips, 5 Trinitron */
    float brightness;                      /* BRIGHTNESS: 0 to 100 */
    float contrast;                        /* CONTRAST: 0 to 100 */
    float gamma;                           /* GAMMA: -1 to 1 */
    float digital_contrast;                /* DIGITAL_CONTRAST: -2 to 2 */
    float black_level_color[3];            /* BLACK_LEVEL_COLOR: 0 to 1 */
    float black_level;                     /* BLACK_LEVEL: 0 to 1 */
    float saturation;                      /* SATURATION: -1 to 1 */
    float color_temperature_kelvin;        /* COLOR_TEMPERATURE_KELVIN */
    float color_temperature_luma_preserve; /* COLOR_TEMPERATURE_LUMA_PRESERVE: 0 to 1 */
    float red_gain;                        /* RED_GAIN: 0 to 2 */
    float green_gain;                      /* GREEN_GAIN */
    float blue_gain;                       /* BLUE_GAIN */
} CRT_AUTO_COLOR;

/* The shader values for the settings with crt-auto on a VGA mode, where
 * 'auto' means P22 phosphors, 7800 K and no black level boost. Output is
 * sRGB. settings can be NULL for DOSBox's defaults */
CRT_AUTO_COLOR crt_auto_vga_color(const CRT_AUTO_SETTINGS* settings);

/* CRT_AUTO_COLOR's fields as the shader's uniforms, for a renderer to set */
typedef enum CRT_AUTO_UNIFORM_TYPE
{
    CRT_AUTO_UNIFORM_INT,
    CRT_AUTO_UNIFORM_FLOAT,
    CRT_AUTO_UNIFORM_VEC3
} CRT_AUTO_UNIFORM_TYPE;

typedef struct CRT_AUTO_UNIFORM
{
    const char* name;
    CRT_AUTO_UNIFORM_TYPE type;
    size_t offset; /* in CRT_AUTO_COLOR */
} CRT_AUTO_UNIFORM;

/* The i'th; NULL past the last */
const CRT_AUTO_UNIFORM* crt_auto_color_uniform(int i);

#endif
