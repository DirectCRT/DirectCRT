/* Unit tests for src/crt_auto.c
 *
 * Build and run: tests\run_tests.cmd (from a Visual Studio developer prompt)
 *            or: make test
 *
 * The expected values follow DOSBox Staging's crt-auto with 'viewport = fit'
 * and 'integer_scaling = auto'.
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
    int line, int mw, int mh, int cw, int ch,
    CRT_SHADER shader, int pps, int rw, int rh, int x, int y, int w, int h)
{
    CRT_AUTO_LAYOUT l = crt_auto_calc_layout(mw, mh, cw, ch);

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
    check_layout(__LINE__, mw, mh, cw, ch, shader, pps, rw, rh, x, y, w, h)

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
}

static void test_doubling_flags()
{
    CRT_AUTO_LAYOUT l = crt_auto_calc_layout(320, 200, 2560, 1440);
    CHECK(l.double_scanned && l.pixel_doubled);

    l = crt_auto_calc_layout(320, 400, 2560, 1440);
    CHECK(!l.double_scanned && l.pixel_doubled);

    l = crt_auto_calc_layout(640, 350, 2560, 1440);
    CHECK(l.double_scanned && !l.pixel_doubled);

    l = crt_auto_calc_layout(640, 480, 2560, 1440);
    CHECK(!l.double_scanned && !l.pixel_doubled);
}

static void test_bad_input()
{
    CRT_AUTO_LAYOUT l = crt_auto_calc_layout(0, 0, 2560, 1440);
    CHECK(l.render_width == 1 && l.render_height == 1);
    CHECK(l.width == 2560 && l.height == 1440);

    l = crt_auto_calc_layout(640, 480, 0, 0);
    CHECK(l.width == 0 && l.height == 0);
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

int main(void)
{
    test_1440p();
    test_1080p();
    test_4k();
    test_windows();
    test_aspect();
    test_doubling_flags();
    test_bad_input();
    test_params();
    test_real_presets_parse();

    printf("crt_auto: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
