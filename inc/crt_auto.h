#ifndef CRT_AUTO_H
#define CRT_AUTO_H

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
} CRT_AUTO_LAYOUT;

/* mode: the game's resolution. canvas: the area the picture goes in */
CRT_AUTO_LAYOUT crt_auto_calc_layout(int mode_width, int mode_height, int canvas_width, int canvas_height);

const char* crt_auto_shader_name(CRT_SHADER shader);


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
    int color_space;                       /* COLOR_SPACE: 0 sRGB */
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

/* DOSBox Staging's defaults with crt-auto on a VGA mode: P22 phosphors,
 * 7800 K, no black level boost, sRGB output */
CRT_AUTO_COLOR crt_auto_vga_color(void);

#endif
