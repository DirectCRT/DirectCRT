/* Unit tests for src/crt_auto.c
 *
 * Build and run: tests\run_tests.cmd (from a Visual Studio developer prompt)
 *            or: make test
 *
 * The expected values follow DOSBox Staging's crt-auto, with its default
 * settings ('viewport = fit', 'integer_scaling = auto', 'aspect = auto')
 * unless a test sets others.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "crt_auto.h"

static int g_failed;
static int g_checks;

#define CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static void check_layout(
    int line, const CRT_AUTO_SETTINGS* settings, const CRT_AUTO_DISPLAY* display,
    int mw, int mh, int cw, int ch,
    CRT_SHADER shader, int pps, int rw, int rh, int x, int y, int w, int h)
{
    CRT_AUTO_LAYOUT l = crt_auto_calc_layout(settings, display, mw, mh, cw, ch);

    g_checks++;

    if (l.shader != shader || l.pixels_per_scanline != pps ||
        l.render_width != rw || l.render_height != rh ||
        l.x != x || l.y != y || l.width != w || l.height != h)
    {
        g_failed++;
        printf(
            "  FAIL line %d: %dx%d in %dx%d\n"
            "       got      %s, %d px/line, render %dx%d, rect %d,%d %dx%d\n"
            "       expected %s, %d px/line, render %dx%d, rect %d,%d %dx%d\n",
            line, mw, mh, cw, ch,
            crt_auto_shader_name(l.shader), l.pixels_per_scanline, l.render_width, l.render_height,
            l.x, l.y, l.width, l.height,
            crt_auto_shader_name(shader), pps, rw, rh, x, y, w, h);
    }
}

#define LAYOUT(mw, mh, cw, ch, shader, pps, rw, rh, x, y, w, h) \
    check_layout(__LINE__, NULL, NULL, mw, mh, cw, ch, shader, pps, rw, rh, x, y, w, h)

/* With settings s (a CRT_AUTO_SETTINGS) on display d (a CRT_AUTO_DISPLAY*,
 * or NULL) */
#define LAYOUT_WITH(s, d, mw, mh, cw, ch, shader, pps, rw, rh, x, y, w, h) \
    check_layout(__LINE__, &(s), d, mw, mh, cw, ch, shader, pps, rw, rh, x, y, w, h)

/* The defaults with one setting changed */
static CRT_AUTO_SETTINGS settings_with(const char* name, const char* value)
{
    CRT_AUTO_SETTINGS s = crt_auto_default_settings();
    CHECK(crt_auto_set(&s, name, value));
    return s;
}

static void test_1440p()
{
    /* 400 double-scanned lines, 3.6x: crt-hyllian vga-1440p, snapped to 3.5x */
    LAYOUT(320, 200, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 400, 347, 20, 1867, 1400);

    /* 480 double-scanned lines, exactly 3x */
    LAYOUT(320, 240, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 480, 320, 0, 1920, 1440);
    LAYOUT(640, 480, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 480, 320, 0, 1920, 1440);

    /* 2.4x: vga-1080p, snapped to 2x */
    LAYOUT(800, 600, 2560, 1440, CRT_SHADER_VGA_1080P, 2, 800, 600, 480, 120, 1600, 1200);

    /* 400x300 is double scanned to 600 lines, but has more than 216, so
     * vga-1080p rather than the fake double scan */
    LAYOUT(400, 300, 2560, 1440, CRT_SHADER_VGA_1080P, 2, 800, 600, 480, 120, 1600, 1200);

    /* 512x384 is doubled to 1024x768: 1.875x, sharp, no snapping */
    LAYOUT(512, 384, 2560, 1440, CRT_SHADER_SHARP, 1, 512, 384, 320, 0, 1920, 1440);
}

static void test_1080p()
{
    /* 400 lines at 2.7x: double-scanned, 200 lines, so the fake double scan
     * shader on the single-scanned image, which is 5.4x: not snapped */
    LAYOUT(320, 200, 1920, 1080, CRT_SHADER_VGA_1080P_FAKE_DOUBLE_SCAN, 2, 640, 200, 240, 0, 1440, 1080);

    /* 240 lines are more than 216: vga-1080p at 2x */
    LAYOUT(320, 240, 1920, 1080, CRT_SHADER_VGA_1080P, 2, 640, 480, 320, 60, 1280, 960);
    LAYOUT(640, 480, 1920, 1080, CRT_SHADER_VGA_1080P, 2, 640, 480, 320, 60, 1280, 960);

    /* 1.8x: sharp fills the height */
    LAYOUT(800, 600, 1920, 1080, CRT_SHADER_SHARP, 1, 800, 600, 240, 0, 1440, 1080);
}

static void test_4k()
{
    /* 5.4x: vga-4k, no snapping from 5x */
    LAYOUT(320, 200, 3840, 2160, CRT_SHADER_HYLLIAN_VGA_4K, 5, 640, 400, 480, 0, 2880, 2160);

    /* 4.5x is a half step */
    LAYOUT(640, 480, 3840, 2160, CRT_SHADER_HYLLIAN_VGA_4K, 4, 640, 480, 480, 0, 2880, 2160);

    /* 3.6x: vga-1440p, snapped to 3.5x */
    LAYOUT(800, 600, 3840, 2160, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 800, 600, 520, 30, 2800, 2100);
}

static void test_windows()
{
    /* A windowed game shown 1:1 */
    LAYOUT(640, 480, 640, 480, CRT_SHADER_SHARP, 1, 640, 480, 0, 0, 640, 480);

    /* A tall, narrow window: the width limits the picture */
    LAYOUT(640, 480, 1000, 1440, CRT_SHADER_SHARP, 1, 640, 480, 0, 345, 1000, 750);

    /* A canvas smaller than the game */
    LAYOUT(800, 600, 400, 300, CRT_SHADER_SHARP, 0, 800, 600, 0, 0, 400, 300);
}

static void test_aspect()
{
    /* 640x400 is shown at 4:3, like VGA's 70 Hz modes */
    LAYOUT(640, 400, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 400, 347, 20, 1867, 1400);

    /* Widescreen modes keep square pixels */
    LAYOUT(1280, 720, 2560, 1440, CRT_SHADER_VGA_1080P, 2, 1280, 720, 0, 0, 2560, 1440);

    /* So does 1280x1024 (5:4), unlike in DOSBox, and 1280x800 (16:10) */
    LAYOUT(1280, 1024, 2560, 1440, CRT_SHADER_SHARP, 1, 1280, 1024, 380, 0, 1800, 1440);
    LAYOUT(1280, 800, 2560, 1440, CRT_SHADER_SHARP, 1, 1280, 800, 128, 0, 2304, 1440);
}

static void test_doubling_flags()
{
    CRT_AUTO_LAYOUT l = crt_auto_calc_layout(NULL, NULL, 320, 200, 2560, 1440);
    CHECK(l.double_scanned && l.pixel_doubled);

    l = crt_auto_calc_layout(NULL, NULL, 320, 400, 2560, 1440);
    CHECK(!l.double_scanned && l.pixel_doubled);

    l = crt_auto_calc_layout(NULL, NULL, 640, 350, 2560, 1440);
    CHECK(l.double_scanned && !l.pixel_doubled);

    l = crt_auto_calc_layout(NULL, NULL, 640, 480, 2560, 1440);
    CHECK(!l.double_scanned && !l.pixel_doubled);
}

static void test_bad_input()
{
    CRT_AUTO_LAYOUT l = crt_auto_calc_layout(NULL, NULL, 0, 0, 2560, 1440);
    CHECK(l.render_width == 1 && l.render_height == 1);
    CHECK(l.width == 2560 && l.height == 1440);

    l = crt_auto_calc_layout(NULL, NULL, 640, 480, 0, 0);
    CHECK(l.width == 0 && l.height == 0);
}

static void test_integer_scaling()
{
    /* off: 3.6x, not snapped to 3.5x */
    CRT_AUTO_SETTINGS s = settings_with("integer_scaling", "off");
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 400, 320, 0, 1920, 1440);

    /* vertical snaps sharp too: 1.8x becomes 1x */
    s = settings_with("integer_scaling", "vertical");
    LAYOUT_WITH(s, NULL, 800, 600, 1920, 1080, CRT_SHADER_SHARP, 1, 800, 600, 560, 240, 800, 600);

    /* vertical has no half steps: 3.6x becomes 3x */
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 400, 480, 120, 1600, 1200);

    /* horizontal: 3x the width, the height follows the 4:3 shape */
    s = settings_with("integer_scaling", "horizontal");
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 400, 320, 0, 1920, 1440);
}

static void test_aspect_settings()
{
    /* square-pixels: 640x400 stays 16:10 */
    CRT_AUTO_SETTINGS s = settings_with("aspect", "square-pixels");
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 400, 160, 20, 2240, 1400);

    /* off is square-pixels too */
    s = settings_with("aspect", "off");
    CHECK(s.aspect == CRT_ASPECT_SQUARE_PIXELS);

    /* stretch: the canvas's 16:9, snapped to 3.5x in height */
    s = settings_with("aspect", "stretch");
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 400, 36, 20, 2489, 1400);
}

static void test_viewport()
{
    /* 3840x2160 at 150%: 2560x1440 logical units */
    CRT_AUTO_DISPLAY d = { 3840, 2160, 1.5f };

    /* In pixels: 2x in 1280x960, centred on the canvas */
    CRT_AUTO_SETTINGS s = settings_with("viewport", "1280x960px");
    LAYOUT_WITH(s, &d, 640, 480, 2560, 1440, CRT_SHADER_VGA_1080P, 2, 640, 480, 640, 240, 1280, 960);

    /* In logical units: 1280x720 is 1920x1080 pixels, 2.25x, so 2x */
    s = settings_with("viewport", "1280x720");
    LAYOUT_WITH(s, &d, 640, 480, 3840, 2160, CRT_SHADER_VGA_1080P, 2, 640, 480, 1280, 600, 1280, 960);

    /* 50% of the desktop: 1920x1080 */
    s = settings_with("viewport", "50%");
    LAYOUT_WITH(s, &d, 640, 480, 3840, 2160, CRT_SHADER_VGA_1080P, 2, 640, 480, 1280, 600, 1280, 960);

    /* Larger than the desktop: rejected, so fit (4.5x) */
    s = settings_with("viewport", "2600x1440");
    LAYOUT_WITH(s, &d, 640, 480, 3840, 2160, CRT_SHADER_HYLLIAN_VGA_4K, 4, 640, 480, 480, 0, 2880, 2160);
    CHECK(crt_auto_calc_layout(&s, &d, 640, 480, 3840, 2160).viewport_rejected);

    s = settings_with("viewport", "2560x1440");
    CHECK(!crt_auto_calc_layout(&s, &d, 640, 480, 3840, 2160).viewport_rejected);

    s = settings_with("viewport", "3840x2160px");
    CHECK(!crt_auto_calc_layout(&s, &d, 640, 480, 3840, 2160).viewport_rejected);

    s = settings_with("viewport", "3841x2160px");
    CHECK(crt_auto_calc_layout(&s, &d, 640, 480, 3840, 2160).viewport_rejected);

    /* Larger than the canvas but not the desktop: the canvas */
    s = settings_with("viewport", "3000x2000px");
    LAYOUT_WITH(s, &d, 640, 480, 2560, 1440, CRT_SHADER_HYLLIAN_VGA_1440P, 3, 640, 480, 320, 0, 1920, 1440);

    /* relative: half of the 1920x1440 4:3 box, 1.5x, so sharp */
    s = settings_with("viewport", "relative 50% 50%");
    LAYOUT_WITH(s, NULL, 640, 480, 2560, 1440, CRT_SHADER_SHARP, 1, 640, 480, 800, 360, 960, 720);

    /* relative can go past the canvas */
    s = settings_with("viewport", "relative 200% 200%");
    LAYOUT_WITH(s, NULL, 640, 480, 1920, 1080, CRT_SHADER_HYLLIAN_VGA_4K, 4, 640, 480, -480, -540, 2880, 2160);
}

static void test_settings_parse()
{
    CRT_AUTO_SETTINGS s = crt_auto_default_settings();

    CHECK(s.aspect == CRT_ASPECT_AUTO);
    CHECK(s.integer_scaling == CRT_INTEGER_SCALING_AUTO);
    CHECK(s.viewport.mode == CRT_VIEWPORT_FIT);
    CHECK(s.image_adjustments == 1);
    CHECK(s.crt_color_profile == CRT_AUTO_SETTING_AUTO);
    CHECK(s.brightness == 45 && s.contrast == 65);
    CHECK(s.black_level == CRT_AUTO_SETTING_AUTO && s.color_temperature == CRT_AUTO_SETTING_AUTO);
    CHECK(s.red_gain == 100 && s.green_gain == 100 && s.blue_gain == 100);

    /* Every setting's default parses */
    for (int i = 0; i < crt_auto_setting_count(); i++)
    {
        CRT_AUTO_SETTINGS t = crt_auto_default_settings();
        CHECK(crt_auto_set(&t, crt_auto_setting_name(i), crt_auto_setting_default(i)));
    }

    CHECK(crt_auto_setting_name(crt_auto_setting_count()) == NULL);

    /* Other names change nothing */
    CHECK(!crt_auto_set(&s, "renderer", "auto"));

    /* Invalid values give the default */
    CHECK(crt_auto_set(&s, "brightness", "80") && s.brightness == 80);
    CHECK(!crt_auto_set(&s, "brightness", "101") && s.brightness == 45);
    CHECK(!crt_auto_set(&s, "brightness", "5o") && s.brightness == 45);
    CHECK(crt_auto_set(&s, "gamma", "-50") && s.gamma == -50);
    CHECK(!crt_auto_set(&s, "gamma", "-51") && s.gamma == 0);
    CHECK(crt_auto_set(&s, "aspect", "stretch") && s.aspect == CRT_ASPECT_STRETCH);
    CHECK(!crt_auto_set(&s, "aspect", "4:3") && s.aspect == CRT_ASPECT_AUTO);
    CHECK(crt_auto_set(&s, "aspect", "on") && s.aspect == CRT_ASPECT_AUTO);
    CHECK(crt_auto_set(&s, "integer_scaling", "false") && s.integer_scaling == CRT_INTEGER_SCALING_OFF);
    CHECK(!crt_auto_set(&s, "integer_scaling", "on") && s.integer_scaling == CRT_INTEGER_SCALING_AUTO);
    CHECK(crt_auto_set(&s, "image_adjustments", "off") && s.image_adjustments == 0);
    CHECK(!crt_auto_set(&s, "image_adjustments", "maybe") && s.image_adjustments == 1);

    /* Spaces around the value and upper case are fine */
    CHECK(crt_auto_set(&s, "contrast", "  70 ") && s.contrast == 70);
    CHECK(crt_auto_set(&s, "crt_color_profile", "Trinitron") && s.crt_color_profile == 5);
    CHECK(crt_auto_set(&s, "crt_color_profile", "none") && s.crt_color_profile == 0);
    CHECK(crt_auto_set(&s, "crt_color_profile", "off") && s.crt_color_profile == 0);
    CHECK(!crt_auto_set(&s, "crt_color_profile", "pvm") && s.crt_color_profile == CRT_AUTO_SETTING_AUTO);

    CHECK(crt_auto_set(&s, "black_level", "20") && s.black_level == 20);
    CHECK(!crt_auto_set(&s, "black_level", "101") && s.black_level == CRT_AUTO_SETTING_AUTO);
    CHECK(crt_auto_set(&s, "color_temperature", "6500") && s.color_temperature == 6500);
    CHECK(!crt_auto_set(&s, "color_temperature", "2000") && s.color_temperature == CRT_AUTO_SETTING_AUTO);

    /* viewport */
    CHECK(crt_auto_set(&s, "viewport", "800x600") && s.viewport.mode == CRT_VIEWPORT_LIMIT);
    CHECK(s.viewport.width == 800 && s.viewport.height == 600);
    CHECK(crt_auto_set(&s, "viewport", "800,600PX") && s.viewport.mode == CRT_VIEWPORT_LIMIT_PX);
    CHECK(crt_auto_set(&s, "viewport", "75%") && s.viewport.mode == CRT_VIEWPORT_DESKTOP);
    CHECK(fabsf(s.viewport.width - 0.75f) < 1e-6f);
    CHECK(crt_auto_set(&s, "viewport", "relative 120% 80%") && s.viewport.mode == CRT_VIEWPORT_RELATIVE);
    CHECK(fabsf(s.viewport.width - 1.2f) < 1e-6f && fabsf(s.viewport.height - 0.8f) < 1e-6f);
    CHECK(crt_auto_set(&s, "viewport", "fit") && s.viewport.mode == CRT_VIEWPORT_FIT);

    static const char* const bad_viewports[] = {
        "0x600", "800x", "800x600x2", "101%", "0.5%", "medium", "50", "relative 10% 80%",
        "relative 100% 301%", "relative 100%", "relative 100% 100% 100%", "relative100% 100%", "",
    };

    for (size_t i = 0; i < sizeof(bad_viewports) / sizeof(bad_viewports[0]); i++)
    {
        CRT_AUTO_SETTINGS t = settings_with("viewport", "800x600");
        CHECK(!crt_auto_set(&t, "viewport", bad_viewports[i]) && t.viewport.mode == CRT_VIEWPORT_FIT);
    }
}

static void test_color()
{
    /* The defaults: DOSBox's values for crt-auto on VGA */
    CRT_AUTO_COLOR c = crt_auto_vga_color(NULL);
    CHECK(c.color_space == 0 && c.enable_adjustments == 1 && c.color_profile == 2);
    CHECK(c.brightness == 45.0f && c.contrast == 65.0f);
    CHECK(c.gamma == 0.0f && c.digital_contrast == 0.0f && c.saturation == 0.0f);
    CHECK(c.black_level == 0.0f && c.color_temperature_kelvin == 7800.0f);
    CHECK(c.color_temperature_luma_preserve == 0.0f);
    CHECK(c.red_gain == 1.0f && c.green_gain == 1.0f && c.blue_gain == 1.0f);

    CRT_AUTO_SETTINGS s = crt_auto_default_settings();
    crt_auto_set(&s, "image_adjustments", "off");
    crt_auto_set(&s, "crt_color_profile", "ebu");
    crt_auto_set(&s, "gamma", "25");
    crt_auto_set(&s, "digital_contrast", "-50");
    crt_auto_set(&s, "black_level", "20");
    crt_auto_set(&s, "saturation", "50");
    crt_auto_set(&s, "color_temperature", "6500");
    crt_auto_set(&s, "color_temperature_luma_preserve", "100");
    crt_auto_set(&s, "red_gain", "150");
    crt_auto_set(&s, "blue_gain", "0");

    c = crt_auto_vga_color(&s);
    CHECK(c.enable_adjustments == 0 && c.color_profile == 1);
    CHECK(c.gamma == 0.5f && c.digital_contrast == -2.0f);
    CHECK(fabsf(c.black_level - 0.2f) < 1e-6f && c.saturation == 1.0f);
    CHECK(c.color_temperature_kelvin == 6500.0f && c.color_temperature_luma_preserve == 1.0f);
    CHECK(c.red_gain == 1.5f && c.green_gain == 1.0f && c.blue_gain == 0.0f);
}

static void test_params()
{
    static const char shader[] =
        "#version 330 core\n"
        "/*\n"
        "#pragma name        Main_Pass1\n"
        "#pragma output_size Viewport\n"
        "#pragma parameter BEAM_PROFILE       \"BEAM PROFILE (BP)\"         0.0  0.0 2.0   1.0\n"
        "#pragma parameter BEAM_MIN_WIDTH     \"Custom [If BP=0.00] MIN\"  0.95  0.0 1.0  0.01\n"
        "#pragma parameter OUTPUT_GAMMA       \"OUTPUT GAMMA\"     2.0   0.0  5.0  0.1\n"
        "*/\n"
        "uniform float BEAM_PROFILE;\n";

    static const char preset[] =
        "[parameters]\r\n"
        "BEAM_MIN_WIDTH      = 1.00\r\n"
        "OUTPUT_GAMMA        = 2.48\r\n"
        "PHOSPHOR_LAYOUT     = 11.00\r\n";

    CRT_AUTO_PARAMS p;
    memset(&p, 0, sizeof(p));

    crt_auto_params_from_shader(&p, shader);
    CHECK(p.count == 3);
    CHECK(crt_auto_params_get(&p, "BEAM_PROFILE", -1) == 0.0f);
    CHECK(fabsf(crt_auto_params_get(&p, "BEAM_MIN_WIDTH", -1) - 0.95f) < 1e-6f);
    CHECK(crt_auto_params_get(&p, "OUTPUT_GAMMA", -1) == 2.0f);
    CHECK(crt_auto_params_get(&p, "MISSING", -1) == -1.0f);

    CHECK(crt_auto_params_apply_preset(&p, preset) == 3);
    CHECK(p.count == 4);
    CHECK(crt_auto_params_get(&p, "BEAM_MIN_WIDTH", -1) == 1.0f);
    CHECK(fabsf(crt_auto_params_get(&p, "OUTPUT_GAMMA", -1) - 2.48f) < 1e-6f);
    CHECK(crt_auto_params_get(&p, "PHOSPHOR_LAYOUT", -1) == 11.0f);
}

static void test_real_presets_parse()
{
    /* The values of DOSBox Staging's vga-4k preset, as in the file */
    static const char preset[] =
        "[parameters]\n"
        "BEAM_PROFILE        = 0.00\n"
        "HFILTER_PROFILE     = 0.00\n"
        "BEAM_MIN_WIDTH      = 1.00\n"
        "BEAM_MAX_WIDTH      = 1.40\n"
        "SCANLINES_STRENGTH  = 0.90\n"
        "COLOR_BOOST         = 3.65\n"
        "SHARPNESS_HACK      = 1.00\n"
        "PHOSPHOR_LAYOUT     = 11.00\n"
        "MASK_INTENSITY      = 0.65\n"
        "CRT_ANTI_RINGING    = 1.00\n"
        "INPUT_GAMMA         = 2.40\n"
        "OUTPUT_GAMMA        = 2.48\n";

    CRT_AUTO_PARAMS p;
    memset(&p, 0, sizeof(p));

    CHECK(crt_auto_params_apply_preset(&p, preset) == 12);
    CHECK(fabsf(crt_auto_params_get(&p, "COLOR_BOOST", -1) - 3.65f) < 1e-6f);
    CHECK(fabsf(crt_auto_params_get(&p, "MASK_INTENSITY", -1) - 0.65f) < 1e-6f);
}

static void test_fixed_shaders()
{
    CRT_AUTO_SETTINGS s = crt_auto_default_settings();

    CHECK(s.shader == CRT_SHADER_SETTING_CRT_AUTO);
    CHECK(crt_auto_set(&s, "shader", "Nearest") && s.shader == CRT_SHADER_SETTING_NEAREST);
    CHECK(!crt_auto_set(&s, "shader", "Nearest neighbor") && s.shader == CRT_SHADER_SETTING_CRT_AUTO);
    CHECK(!crt_auto_set(&s, "shader", "crt/vga-1080p") && s.shader == CRT_SHADER_SETTING_CRT_AUTO);

    /* sharp and nearest get the game's picture as it is, bilinear the doubled
     * one; none of them snaps with integer_scaling = auto */
    s = settings_with("shader", "sharp");
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_SHARP, 3, 320, 200, 320, 0, 1920, 1440);
    LAYOUT_WITH(s, NULL, 640, 480, 3840, 2160, CRT_SHADER_SHARP, 4, 640, 480, 480, 0, 2880, 2160);

    s = settings_with("shader", "nearest");
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_NEAREST, 3, 320, 200, 320, 0, 1920, 1440);
    LAYOUT_WITH(s, NULL, 640, 480, 1920, 1080, CRT_SHADER_NEAREST, 2, 640, 480, 240, 0, 1440, 1080);

    s = settings_with("shader", "bilinear");
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_BILINEAR, 3, 640, 400, 320, 0, 1920, 1440);
    LAYOUT_WITH(s, NULL, 640, 480, 1920, 1080, CRT_SHADER_BILINEAR, 2, 640, 480, 240, 0, 1440, 1080);

    /* A small window: still the fixed shader */
    LAYOUT_WITH(s, NULL, 640, 480, 640, 480, CRT_SHADER_BILINEAR, 1, 640, 480, 0, 0, 640, 480);

    /* integer_scaling = vertical snaps every shader */
    s = settings_with("shader", "nearest");
    crt_auto_set(&s, "integer_scaling", "vertical");
    LAYOUT_WITH(s, NULL, 320, 200, 2560, 1440, CRT_SHADER_NEAREST, 3, 320, 200, 347, 20, 1867, 1400);
}

int main(void)
{
    test_1440p();
    test_1080p();
    test_4k();
    test_windows();
    test_aspect();
    test_doubling_flags();
    test_bad_input();
    test_integer_scaling();
    test_aspect_settings();
    test_viewport();
    test_settings_parse();
    test_color();
    test_params();
    test_real_presets_parse();
    test_fixed_shaders();

    printf("crt_auto: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
