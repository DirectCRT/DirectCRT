/* Unit tests for src/display_settings.c
 *
 * Build and run: tests\run_tests.cmd (from a Visual Studio developer prompt)
 *
 * The window_size and window_position values follow DOSBox Staging's
 * tests/window_geometry_tests.cpp where it has the same case.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "display_settings.h"

static int g_failed;
static int g_checks;

#define CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

#define CHECK_STR(a, b) \
    do { \
        const char* a_ = (a); const char* b_ = (b); \
        g_checks++; \
        if ((a_ == NULL) != (b_ == NULL) || (a_ && strcmp(a_, b_) != 0)) { \
            g_failed++; \
            printf("  FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, \
                a_ ? a_ : "(null)", b_ ? b_ : "(null)"); \
        } \
    } while (0)

static int feq(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static void test_display(void)
{
    DISPLAY_MODE m = DISPLAY_WINDOW;

    CHECK(ds_parse_display("auto", &m) && m == DISPLAY_AUTO);
    CHECK(ds_parse_display("Window", &m) && m == DISPLAY_WINDOW);
    CHECK(ds_parse_display(" FULLSCREEN ", &m) && m == DISPLAY_FULLSCREEN);
    CHECK(ds_parse_display("original", &m) && m == DISPLAY_ORIGINAL);

    m = DISPLAY_WINDOW;
    CHECK(!ds_parse_display("", &m) && m == DISPLAY_WINDOW);
    CHECK(!ds_parse_display(NULL, &m) && m == DISPLAY_WINDOW);
    CHECK(!ds_parse_display("windowed", &m) && m == DISPLAY_WINDOW);
    CHECK(!ds_parse_display("true", &m));

    CHECK_STR(ds_display_name(DISPLAY_ORIGINAL), "original");

    /* The table in docs/development-plan.md */
    CHECK(ds_display_start(DISPLAY_AUTO, 1) == DISPLAY_KIND_DESKTOP);
    CHECK(ds_display_start(DISPLAY_AUTO, 0) == DISPLAY_KIND_WINDOW);
    CHECK(ds_display_start(DISPLAY_WINDOW, 1) == DISPLAY_KIND_WINDOW);
    CHECK(ds_display_start(DISPLAY_WINDOW, 0) == DISPLAY_KIND_WINDOW);
    CHECK(ds_display_start(DISPLAY_FULLSCREEN, 1) == DISPLAY_KIND_DESKTOP);
    CHECK(ds_display_start(DISPLAY_FULLSCREEN, 0) == DISPLAY_KIND_DESKTOP);
    CHECK(ds_display_start(DISPLAY_ORIGINAL, 1) == DISPLAY_KIND_MODE_SWITCH);
    CHECK(ds_display_start(DISPLAY_ORIGINAL, 0) == DISPLAY_KIND_WINDOW);

    CHECK(ds_display_fullscreen_kind(DISPLAY_AUTO) == DISPLAY_KIND_DESKTOP);
    CHECK(ds_display_fullscreen_kind(DISPLAY_WINDOW) == DISPLAY_KIND_DESKTOP);
    CHECK(ds_display_fullscreen_kind(DISPLAY_FULLSCREEN) == DISPLAY_KIND_DESKTOP);
    CHECK(ds_display_fullscreen_kind(DISPLAY_ORIGINAL) == DISPLAY_KIND_MODE_SWITCH);
}

static void test_bool_and_remember(void)
{
    int b = 5;

    CHECK(ds_parse_bool("on", &b) && b == 1);
    CHECK(ds_parse_bool("OFF", &b) && b == 0);
    CHECK(ds_parse_bool("true", &b) && b == 1);
    CHECK(ds_parse_bool("false", &b) && b == 0);
    CHECK(ds_parse_bool("1", &b) && b == 1);
    CHECK(ds_parse_bool("0", &b) && b == 0);
    b = 5;
    CHECK(!ds_parse_bool("2", &b) && b == 5);
    CHECK(!ds_parse_bool("", &b) && b == 5);

    REMEMBER_WINDOW r = REMEMBER_WINDOW_GAME;

    CHECK(ds_parse_remember_window("off", &r) && r == REMEMBER_WINDOW_OFF);
    CHECK(ds_parse_remember_window("Global", &r) && r == REMEMBER_WINDOW_GLOBAL);
    CHECK(ds_parse_remember_window("game", &r) && r == REMEMBER_WINDOW_GAME);
    CHECK(!ds_parse_remember_window("1", &r) && r == REMEMBER_WINDOW_GAME);
    CHECK_STR(ds_remember_window_name(REMEMBER_WINDOW_GLOBAL), "global");
}

static void test_window_size_parse(void)
{
    WINDOW_SIZE s;

    CHECK(ds_parse_window_size("small", &s) && !s.unscaled && s.unit == GEOMETRY_PERCENT &&
        feq(s.w, 50.0f * 4 / 3) && feq(s.h, 50.0f));
    CHECK(ds_parse_window_size("s", &s) && feq(s.h, 50.0f));
    CHECK(ds_parse_window_size("medium", &s) && feq(s.w, 74.0f * 4 / 3) && feq(s.h, 74.0f));
    CHECK(ds_parse_window_size("Default", &s) && feq(s.h, 74.0f));
    CHECK(ds_parse_window_size("m", &s) && feq(s.h, 74.0f));
    CHECK(ds_parse_window_size("LARGE", &s) && feq(s.w, 120.0f) && feq(s.h, 90.0f));
    CHECK(ds_parse_window_size("l", &s) && feq(s.h, 90.0f));

    CHECK(ds_parse_window_size("unscaled", &s) && s.unscaled);

    CHECK(ds_parse_window_size("1024x768", &s) && !s.unscaled && s.unit == GEOMETRY_LOGICAL &&
        feq(s.w, 1024) && feq(s.h, 768));
    CHECK(ds_parse_window_size("1024,768", &s) && s.unit == GEOMETRY_LOGICAL && feq(s.h, 768));
    CHECK(ds_parse_window_size("1920x1080px", &s) && s.unit == GEOMETRY_PIXELS &&
        feq(s.w, 1920) && feq(s.h, 1080));
    CHECK(ds_parse_window_size("1920X1080PX", &s) && s.unit == GEOMETRY_PIXELS);
    CHECK(ds_parse_window_size("80x60%", &s) && s.unit == GEOMETRY_PERCENT &&
        feq(s.w, 80) && feq(s.h, 60));
    CHECK(ds_parse_window_size("12.5x10.25%", &s) && feq(s.w, 12.5f) && feq(s.h, 10.25f));
    CHECK(ds_parse_window_size("65535x65535", &s));
    CHECK(ds_parse_window_size("1000x1000%", &s));

    s.w = 1.0f;
    CHECK(!ds_parse_window_size("", &s) && feq(s.w, 1.0f));
    CHECK(!ds_parse_window_size(NULL, &s));
    CHECK(!ds_parse_window_size("huge", &s));
    CHECK(!ds_parse_window_size("1024", &s));
    CHECK(!ds_parse_window_size("1024x", &s));
    CHECK(!ds_parse_window_size("x768", &s));
    CHECK(!ds_parse_window_size("1024x768x2", &s));
    CHECK(!ds_parse_window_size("0x768", &s));
    CHECK(!ds_parse_window_size("1024x0px", &s));
    CHECK(!ds_parse_window_size("-1024x768", &s));
    CHECK(!ds_parse_window_size("1024.5x768", &s));       /* fractions only in % */
    CHECK(!ds_parse_window_size("65536x768", &s));
    CHECK(!ds_parse_window_size("1001x50%", &s));
    CHECK(!ds_parse_window_size("1e2x50%", &s));
    CHECK(!ds_parse_window_size("1024x768pt", &s));
    CHECK(!ds_parse_window_size("1024 x 768", &s));
    CHECK(feq(s.w, 1.0f));
}

static void client(const char* value, int dw, int dh, float scale, int gw, int gh, int* w, int* h)
{
    WINDOW_SIZE s;
    *w = *h = -1;

    if (ds_parse_window_size(value, &s))
        ds_window_client_size(&s, dw, dh, scale, gw, gh, w, h);
}

static void test_window_client_size(void)
{
    int w, h;

    /* The plan's example: 3840x2160 at 150%, medium */
    client("default", 3840, 2160, 1.5f, 640, 480, &w, &h);
    CHECK(w == 2131 && h == 1598);

    client("small", 1920, 1080, 1.0f, 640, 480, &w, &h);
    CHECK(w == 720 && h == 540);

    client("medium", 1920, 1080, 1.0f, 640, 480, &w, &h);
    CHECK(w == 1066 && h == 799);

    client("large", 1920, 1080, 1.0f, 640, 480, &w, &h);
    CHECK(w == 1296 && h == 972);

    /* Percentages are of the desktop height, for both */
    client("50x50%", 2560, 1440, 1.0f, 640, 480, &w, &h);
    CHECK(w == 720 && h == 720);

    /* Logical units scale with the display */
    client("1024x768", 3840, 2160, 1.5f, 640, 480, &w, &h);
    CHECK(w == 1536 && h == 1152);

    client("1024x768", 1920, 1080, 1.0f, 640, 480, &w, &h);
    CHECK(w == 1024 && h == 768);

    /* Pixels don't */
    client("1920x1080px", 3840, 2160, 1.5f, 640, 480, &w, &h);
    CHECK(w == 1920 && h == 1080);

    client("1919x1079px", 3840, 2160, 1.25f, 640, 480, &w, &h);
    CHECK(w == 1919 && h == 1079);

    /* The minimum, 320x200 logical */
    client("100x100", 1920, 1080, 1.0f, 640, 480, &w, &h);
    CHECK(w == 320 && h == 200);

    client("100x100px", 3840, 2160, 2.0f, 640, 480, &w, &h);
    CHECK(w == 640 && h == 400);

    client("400x100px", 1920, 1080, 1.0f, 640, 480, &w, &h);
    CHECK(w == 400 && h == 200);

    client("1x1%", 1920, 1080, 1.0f, 640, 480, &w, &h);
    CHECK(w == 320 && h == 200);

    /* unscaled is the game's resolution, without the minimum */
    client("unscaled", 3840, 2160, 1.5f, 640, 480, &w, &h);
    CHECK(w == 640 && h == 480);

    client("unscaled", 1920, 1080, 1.0f, 160, 100, &w, &h);
    CHECK(w == 160 && h == 100);

    /* A bad scale is 1 */
    client("1024x768", 1920, 1080, 0.0f, 640, 480, &w, &h);
    CHECK(w == 1024 && h == 768);
}

static void test_window_position(void)
{
    WINDOW_POSITION p;
    int x, y;

    CHECK(ds_parse_window_position("auto", &p) && p.is_auto);
    CHECK(ds_parse_window_position("AUTO", &p) && p.is_auto);

    CHECK(ds_parse_window_position("100,50", &p) && !p.is_auto && p.unit == GEOMETRY_LOGICAL &&
        feq(p.x, 100) && feq(p.y, 50));
    CHECK(ds_parse_window_position("0,0", &p) && !p.is_auto && feq(p.x, 0) && feq(p.y, 0));
    CHECK(ds_parse_window_position("100,50px", &p) && p.unit == GEOMETRY_PIXELS);
    CHECK(ds_parse_window_position("10,12.5%", &p) && p.unit == GEOMETRY_PERCENT && feq(p.y, 12.5f));

    p.x = 7.0f;
    CHECK(!ds_parse_window_position("", &p) && feq(p.x, 7.0f));
    CHECK(!ds_parse_window_position("100x50", &p));       /* sizes use x, positions only , */
    CHECK(!ds_parse_window_position("100", &p));
    CHECK(!ds_parse_window_position("-100,50", &p));
    CHECK(!ds_parse_window_position("100,50,1", &p));
    CHECK(!ds_parse_window_position("center", &p));
    CHECK(feq(p.x, 7.0f));

    ds_parse_window_position("100,50", &p);
    ds_window_position_pixels(&p, 3840, 2160, 1.5f, &x, &y);
    CHECK(x == 150 && y == 75);

    ds_parse_window_position("100,50px", &p);
    ds_window_position_pixels(&p, 3840, 2160, 1.5f, &x, &y);
    CHECK(x == 100 && y == 50);

    /* Percentages of the desktop's width and height */
    ds_parse_window_position("25,50%", &p);
    ds_window_position_pixels(&p, 3840, 2160, 1.5f, &x, &y);
    CHECK(x == 960 && y == 1080);
}

static void test_format(void)
{
    char buf[32];
    WINDOW_SIZE s;
    WINDOW_POSITION p;

    ds_format_size_pixels(buf, sizeof(buf), 1280, 960);
    CHECK_STR(buf, "1280x960px");
    CHECK(ds_parse_window_size(buf, &s) && s.unit == GEOMETRY_PIXELS && feq(s.w, 1280) && feq(s.h, 960));

    ds_format_position_pixels(buf, sizeof(buf), 12, 34);
    CHECK_STR(buf, "12,34px");
    CHECK(ds_parse_window_position(buf, &p) && p.unit == GEOMETRY_PIXELS && feq(p.x, 12) && feq(p.y, 34));
}

static void test_legacy(void)
{
    char buf[32];

    /* The table in docs/development-plan.md */
    CHECK_STR(ds_legacy_display("auto", NULL), "auto");
    CHECK_STR(ds_legacy_display("auto", "true"), "auto");
    CHECK_STR(ds_legacy_display("auto", "false"), "auto");
    CHECK_STR(ds_legacy_display("true", NULL), "window");
    CHECK_STR(ds_legacy_display("true", "false"), "window");
    CHECK_STR(ds_legacy_display("true", "true"), "fullscreen");
    CHECK_STR(ds_legacy_display("1", "1"), "fullscreen");
    CHECK_STR(ds_legacy_display("false", NULL), "fullscreen");
    CHECK_STR(ds_legacy_display("false", "true"), "fullscreen");
    /* Never original */
    CHECK_STR(ds_legacy_display("false", "false"), "fullscreen");
    CHECK_STR(ds_legacy_display("0", "0"), "fullscreen");
    CHECK_STR(ds_legacy_display(NULL, "true"), NULL);
    CHECK_STR(ds_legacy_display("", "true"), NULL);
    CHECK_STR(ds_legacy_display("maybe", NULL), NULL);

    CHECK(ds_legacy_window_size("1024", "768", buf, sizeof(buf)));
    CHECK_STR(buf, "1024x768px");
    CHECK(!ds_legacy_window_size("0", "0", buf, sizeof(buf)));
    CHECK(!ds_legacy_window_size("1024", "0", buf, sizeof(buf)));
    CHECK(!ds_legacy_window_size("1024", NULL, buf, sizeof(buf)));
    CHECK(!ds_legacy_window_size("", "", buf, sizeof(buf)));
    CHECK(!ds_legacy_window_size("-5", "768", buf, sizeof(buf)));

    CHECK(ds_legacy_window_position("100", "200", buf, sizeof(buf)));
    CHECK_STR(buf, "100,200px");
    CHECK(ds_legacy_window_position("-32000", "-32000", buf, sizeof(buf)));
    CHECK_STR(buf, "auto");
    CHECK(ds_legacy_window_position("0", "0", buf, sizeof(buf)));
    CHECK_STR(buf, "0,0px");
    CHECK(ds_legacy_window_position("-1900", "100", buf, sizeof(buf)));
    CHECK_STR(buf, "auto");
    CHECK(!ds_legacy_window_position(NULL, "100", buf, sizeof(buf)));

    CHECK_STR(ds_legacy_remember_window("0"), "off");
    CHECK_STR(ds_legacy_remember_window("1"), "global");
    CHECK_STR(ds_legacy_remember_window("2"), "game");
    CHECK_STR(ds_legacy_remember_window("3"), NULL);
    CHECK_STR(ds_legacy_remember_window(""), NULL);
    CHECK_STR(ds_legacy_remember_window(NULL), NULL);

    CHECK_STR(ds_legacy_shader("Nearest neighbor"), "nearest");
    CHECK_STR(ds_legacy_shader("Bilinear"), "bilinear");
    CHECK_STR(ds_legacy_shader("bilinear"), "bilinear");
    CHECK_STR(ds_legacy_shader("Bicubic"), "crt-auto");
    CHECK_STR(ds_legacy_shader("Lanczos"), "crt-auto");
    CHECK_STR(ds_legacy_shader("Shaders\\xbr\\xbr-lv2.glsl"), "crt-auto");
    CHECK_STR(ds_legacy_shader("CRT-Auto"), "crt-auto");
    CHECK_STR(ds_legacy_shader("sharp"), "sharp");
    CHECK_STR(ds_legacy_shader("NEAREST"), "nearest");
    CHECK_STR(ds_legacy_shader(""), NULL);

    CHECK_STR(ds_legacy_renderer("openglcore"), "auto");
    CHECK_STR(ds_legacy_renderer("direct3d9"), "auto");
    CHECK_STR(ds_legacy_renderer("Direct3D9on12"), "auto");
    CHECK_STR(ds_legacy_renderer("opengl"), NULL);
    CHECK_STR(ds_legacy_renderer("direct3d11"), NULL);
    CHECK_STR(ds_legacy_renderer("gdi"), "auto");
    CHECK_STR(ds_legacy_renderer("Software"), "auto");
}

int main(void)
{
    test_display();
    test_bool_and_remember();
    test_window_size_parse();
    test_window_client_size();
    test_window_position();
    test_format();
    test_legacy();

    printf("display_settings: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
