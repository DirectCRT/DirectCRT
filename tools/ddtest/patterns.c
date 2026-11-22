/*
 * Test patterns. Every pattern is generated for the canvas size it's given,
 * so any resolution works.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ddtest.h"

static const char* PATTERN_NAMES[PAT_COUNT] = {
    "Colour bars",
    "Grey and colour ramps",
    "Grid (16 x 12 cells)",
    "Pixel patterns",
    "Geometry / aspect ratio",
    "Dither patterns",
    "Text",
    "Motion",
    "Interlaced (alternate black lines)",
    "Palette cycling",
    "White field",
    "Test image (built-in, 1:1)",
    "Image file (1:1)",
};

static const char* PATTERN_SHORT_NAMES[PAT_COUNT] = {
    "bars", "ramps", "grid", "pixels", "geometry", "dither",
    "text", "motion", "interlaced", "palette", "white", "testimage", "image",
};

const char* pattern_name(int id)
{
    return (id >= 0 && id < PAT_COUNT) ? PATTERN_NAMES[id] : "?";
}

const char* pattern_short_name(int id)
{
    return (id >= 0 && id < PAT_COUNT) ? PATTERN_SHORT_NAMES[id] : "?";
}

int pattern_from_short_name(const char* name)
{
    for (int i = 0; i < PAT_COUNT; i++)
    {
        if (_stricmp(name, PATTERN_SHORT_NAMES[i]) == 0)
            return i;
    }
    return -1;
}

/* ---------------------------------------------------------------------- */
/* 8-bit palette                                                           */

static uint8_t g_lut[32768];

void palette_build(uint32_t pal[256])
{
    for (int i = 0; i < 216; i++)
        pal[i] = COL((i / 36) * 51, ((i / 6) % 6) * 51, (i % 6) * 51);

    for (int i = 216; i < 256; i++)
    {
        int v = (i - 216) * 255 / 39;
        pal[i] = COL(v, v, v);
    }
}

void palette_build_lut(const uint32_t pal[256])
{
    for (int idx = 0; idx < 32768; idx++)
    {
        int r = (((idx >> 10) & 31) * 255 + 15) / 31;
        int g = (((idx >> 5) & 31) * 255 + 15) / 31;
        int b = ((idx & 31) * 255 + 15) / 31;

        int best = 0;
        long best_d = LONG_MAX;

        /* Searching downwards makes ties go to the greys (216-255), which
           palette cycling leaves alone, so white/grey/black overlays keep
           their colours while the palette cycles. */
        for (int i = 255; i >= 0; i--)
        {
            int dr = r - (int)COL_R(pal[i]);
            int dg = g - (int)COL_G(pal[i]);
            int db = b - (int)COL_B(pal[i]);
            long d = (long)dr * dr * 3 + (long)dg * dg * 4 + (long)db * db * 2;

            if (d < best_d)
            {
                best_d = d;
                best = i;
            }
        }

        g_lut[idx] = (uint8_t)best;
    }
}

uint8_t palette_lookup(uint32_t c)
{
    return g_lut[(((c >> 19) & 31) << 10) | (((c >> 11) & 31) << 5) | ((c >> 3) & 31)];
}

/* ---------------------------------------------------------------------- */
/* Drawing helpers                                                         */

void cv_put(Canvas* c, int x, int y, uint32_t color)
{
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h)
        return;

    c->px[y * c->w + x] = color;

    if (c->use_idx)
        c->idx[y * c->w + x] = palette_lookup(color);
}

void cv_rect(Canvas* c, int x0, int y0, int x1, int y1, uint32_t color)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;

    if (x0 >= x1 || y0 >= y1)
        return;

    uint8_t index = c->use_idx ? palette_lookup(color) : 0;

    for (int y = y0; y < y1; y++)
    {
        uint32_t* row = &c->px[y * c->w];

        for (int x = x0; x < x1; x++)
            row[x] = color;

        if (c->use_idx)
            memset(&c->idx[y * c->w + x0], index, (size_t)(x1 - x0));
    }
}

void cv_fill(Canvas* c, uint32_t color)
{
    cv_rect(c, 0, 0, c->w, c->h, color);
}

void cv_hline(Canvas* c, int x0, int x1, int y, uint32_t color)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    cv_rect(c, x0, y, x1 + 1, y + 1, color);
}

void cv_vline(Canvas* c, int x, int y0, int y1, uint32_t color)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    cv_rect(c, x, y0, x + 1, y1 + 1, color);
}

void cv_outline(Canvas* c, int x0, int y0, int x1, int y1, uint32_t color)
{
    cv_hline(c, x0, x1, y0, color);
    cv_hline(c, x0, x1, y1, color);
    cv_vline(c, x0, y0, y1, color);
    cv_vline(c, x1, y0, y1, color);
}

static void ellipse1(Canvas* c, double cx, double cy, double rx, double ry, uint32_t color)
{
    if (rx < 0.5 || ry < 0.5)
        return;

    /* Plot along both axes so steep and flat parts have no gaps */
    for (int y = (int)floor(cy - ry); y <= (int)ceil(cy + ry); y++)
    {
        double dy = (y - cy) / ry;
        if (dy < -1.0 || dy > 1.0)
            continue;

        double dx = rx * sqrt(1.0 - dy * dy);
        cv_put(c, (int)lround(cx - dx), y, color);
        cv_put(c, (int)lround(cx + dx), y, color);
    }

    for (int x = (int)floor(cx - rx); x <= (int)ceil(cx + rx); x++)
    {
        double dx = (x - cx) / rx;
        if (dx < -1.0 || dx > 1.0)
            continue;

        double dy = ry * sqrt(1.0 - dx * dx);
        cv_put(c, x, (int)lround(cy - dy), color);
        cv_put(c, x, (int)lround(cy + dy), color);
    }
}

void cv_ellipse(Canvas* c, double cx, double cy, double rx, double ry, uint32_t color, int thickness)
{
    for (int t = 0; t < thickness; t++)
        ellipse1(c, cx, cy, rx - t, ry - t, color);
}

double cv_screen_aspect(const Canvas* c)
{
    return c->screen_aspect > 0 ? c->screen_aspect : 4.0 / 3.0;
}

double cv_par(const Canvas* c)
{
    /* A screen of aspect a shows w pixels across a units and h pixels across 1 */
    return cv_screen_aspect(c) / ((double)c->w / (double)c->h);
}

/* "16:9" for the common shapes, otherwise "1.85:1" */
static void aspect_name(double a, char* buf, int len)
{
    static const struct { int w, h; } named[] = {
        { 4, 3 }, { 16, 9 }, { 16, 10 }, { 5, 4 }, { 3, 2 }, { 21, 9 }, { 32, 9 }, { 1, 1 },
    };

    for (int i = 0; i < (int)(sizeof(named) / sizeof(named[0])); i++)
    {
        if (fabs(a - (double)named[i].w / named[i].h) < 0.01)
        {
            _snprintf(buf, len - 1, "%d:%d", named[i].w, named[i].h);
            buf[len - 1] = '\0';
            return;
        }
    }

    _snprintf(buf, len - 1, "%.2f:1", a);
    buf[len - 1] = '\0';
}

int cv_text_size(const Canvas* c)
{
    int s = c->h / 28;
    if (s < 8) s = 8;
    if (s > 36) s = 36;
    return s;
}

/* ---------------------------------------------------------------------- */
/* Text, rendered with GDI into a private bitmap and copied as solid pixels */

#define TEXT_BUF_W 4096
#define TEXT_BUF_H 256

static HDC g_text_dc;
static HBITMAP g_text_bmp;
static HGDIOBJ g_text_old_bmp;
static uint32_t* g_text_bits;

typedef struct { int height, width; HFONT font; } FontEntry;
static FontEntry g_fonts[16];
static int g_font_count;

static BOOL text_init(void)
{
    if (g_text_dc)
        return g_text_bits != NULL;

    g_text_dc = CreateCompatibleDC(NULL);
    if (!g_text_dc)
        return FALSE;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = TEXT_BUF_W;
    bi.bmiHeader.biHeight = -TEXT_BUF_H; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    g_text_bmp = CreateDIBSection(g_text_dc, &bi, DIB_RGB_COLORS, (void**)&g_text_bits, NULL, 0);
    if (!g_text_bmp)
        return FALSE;

    g_text_old_bmp = SelectObject(g_text_dc, g_text_bmp);
    SetBkMode(g_text_dc, OPAQUE);
    SetBkColor(g_text_dc, RGB(0, 0, 0));
    SetTextColor(g_text_dc, RGB(255, 255, 255));
    return TRUE;
}

static HFONT get_font(int height, int width)
{
    for (int i = 0; i < g_font_count; i++)
    {
        if (g_fonts[i].height == height && g_fonts[i].width == width)
            return g_fonts[i].font;
    }

    if (g_font_count == (int)(sizeof(g_fonts) / sizeof(g_fonts[0])))
    {
        SelectObject(g_text_dc, GetStockObject(SYSTEM_FONT));
        for (int i = 0; i < g_font_count; i++)
            DeleteObject(g_fonts[i].font);
        g_font_count = 0;
    }

    /* Non-antialiased so the copied pixels are crisp at any size */
    HFONT f = CreateFontA(
        -height, width, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        FIXED_PITCH | FF_MODERN, "Lucida Console");

    g_fonts[g_font_count].height = height;
    g_fonts[g_font_count].width = width;
    g_fonts[g_font_count].font = f;
    g_font_count++;
    return f;
}

SIZE cv_text_extent(int height, int char_width, const char* s)
{
    SIZE sz = { 0, 0 };

    if (!text_init() || !*s)
        return sz;

    SelectObject(g_text_dc, get_font(height, char_width));
    GetTextExtentPoint32A(g_text_dc, s, (int)strlen(s), &sz);
    return sz;
}

int cv_text_ex(Canvas* c, int x, int y, int height, int char_width, uint32_t color, const char* s)
{
    int len = (int)strlen(s);

    if (!text_init() || len == 0)
        return 0;

    SelectObject(g_text_dc, get_font(height, char_width));

    SIZE sz;
    GetTextExtentPoint32A(g_text_dc, s, len, &sz);

    int tw = sz.cx < TEXT_BUF_W ? sz.cx : TEXT_BUF_W;
    int th = sz.cy < TEXT_BUF_H ? sz.cy : TEXT_BUF_H;

    RECT r = { 0, 0, tw, th };
    ExtTextOutA(g_text_dc, 0, 0, ETO_OPAQUE | ETO_CLIPPED, &r, s, len, NULL);
    GdiFlush();

    for (int yy = 0; yy < th; yy++)
    {
        const uint32_t* row = &g_text_bits[yy * TEXT_BUF_W];

        for (int xx = 0; xx < tw; xx++)
        {
            if (row[xx] & 0x80)
                cv_put(c, x + xx, y + yy, color);
        }
    }

    return sz.cx;
}

int cv_text(Canvas* c, int x, int y, int height, uint32_t color, const char* s)
{
    return cv_text_ex(c, x, y, height, 0, color, s);
}

int cv_label(Canvas* c, int x, int y, int height, uint32_t fg, uint32_t bg, const char* s)
{
    SIZE sz = cv_text_extent(height, 0, s);
    cv_rect(c, x - 2, y - 1, x + sz.cx + 2, y + sz.cy + 1, bg);
    cv_text(c, x, y, height, fg, s);
    return sz.cy + 2;
}

void cv_shutdown(void)
{
    if (g_text_dc)
    {
        SelectObject(g_text_dc, GetStockObject(SYSTEM_FONT));
        for (int i = 0; i < g_font_count; i++)
            DeleteObject(g_fonts[i].font);
        g_font_count = 0;

        if (g_text_old_bmp)
            SelectObject(g_text_dc, g_text_old_bmp);
        if (g_text_bmp)
            DeleteObject(g_text_bmp);
        DeleteDC(g_text_dc);
    }

    g_text_dc = NULL;
    g_text_bmp = NULL;
    g_text_bits = NULL;
}

/* ---------------------------------------------------------------------- */
/* Patterns                                                                */

static int small_text(const Canvas* c)
{
    int s = cv_text_size(c) * 3 / 4;
    return s < 8 ? 8 : s;
}

static uint32_t average(uint32_t a, uint32_t b)
{
    return COL((COL_R(a) + COL_R(b)) / 2, (COL_G(a) + COL_G(b)) / 2, (COL_B(a) + COL_B(b)) / 2);
}

/* SMPTE-style bars, plus full-strength colours and near-black steps for
   checking black level */
static void pat_color_bars(Canvas* c)
{
    static const uint32_t top[7] = {
        COL(191, 191, 191), COL(191, 191, 0), COL(0, 191, 191), COL(0, 191, 0),
        COL(191, 0, 191), COL(191, 0, 0), COL(0, 0, 191),
    };
    static const uint32_t mid[7] = {
        COL(0, 0, 191), COL(0, 0, 0), COL(191, 0, 191), COL(0, 0, 0),
        COL(0, 191, 191), COL(0, 0, 0), COL(191, 191, 191),
    };
    static const uint32_t bottom[8] = {
        COL(255, 0, 0), COL(0, 255, 0), COL(0, 0, 255), COL(255, 255, 255),
        COL(0, 0, 0), COL(8, 8, 8), COL(16, 16, 16), COL(24, 24, 24),
    };
    static const char* bottom_labels[8] = { NULL, NULL, NULL, NULL, "0", "8", "16", "24" };

    int w = c->w, h = c->h;
    int y1 = h * 2 / 3, y2 = h * 3 / 4;

    for (int i = 0; i < 7; i++)
    {
        int x0 = w * i / 7, x1 = w * (i + 1) / 7;
        cv_rect(c, x0, 0, x1, y1, top[i]);
        cv_rect(c, x0, y1, x1, y2, mid[i]);
    }

    int ts = small_text(c);

    for (int i = 0; i < 8; i++)
    {
        int x0 = w * i / 8, x1 = w * (i + 1) / 8;
        cv_rect(c, x0, y2, x1, h, bottom[i]);

        if (bottom_labels[i])
            cv_text(c, x0 + 2, h - ts - 2, ts, COL(96, 96, 96), bottom_labels[i]);
    }
}

static void pat_ramps(Canvas* c)
{
    int w = c->w, h = c->h;
    int bands = 6;

    for (int b = 0; b < bands; b++)
    {
        int y0 = h * b / bands, y1 = h * (b + 1) / bands;

        for (int x = 0; x < w; x++)
        {
            int v = w > 1 ? x * 255 / (w - 1) : 0;
            uint32_t col;

            switch (b)
            {
            case 0: /* 16 grey steps */
                col = COL((x * 16 / w) * 17, (x * 16 / w) * 17, (x * 16 / w) * 17);
                break;
            case 1: col = COL(v, v, v); break;
            case 2: col = COL(v, 0, 0); break;
            case 3: col = COL(0, v, 0); break;
            case 4: col = COL(0, 0, v); break;
            default: /* 16 near-black steps, 0 to 30 */
                col = COL((x * 16 / w) * 2, (x * 16 / w) * 2, (x * 16 / w) * 2);
                break;
            }

            for (int y = y0; y < y1; y++)
                c->px[y * w + x] = col;
        }
    }
}

static void pat_grid(Canvas* c)
{
    int w = c->w, h = c->h;
    uint32_t line = COL(200, 200, 200);

    cv_fill(c, BLACK);

    for (int i = 0; i <= 16; i++)
        cv_vline(c, i == 16 ? w - 1 : w * i / 16, 0, h - 1, line);

    for (int j = 0; j <= 12; j++)
        cv_hline(c, 0, w - 1, j == 12 ? h - 1 : h * j / 12, line);

    /* Centre lines, then a red border on the outermost pixels to check for cropping */
    cv_vline(c, w / 2, 0, h - 1, COL(0, 255, 0));
    cv_hline(c, 0, w - 1, h / 2, COL(0, 255, 0));
    cv_outline(c, 0, 0, w - 1, h - 1, COL(255, 0, 0));

    int ts = small_text(c);
    char text[64];
    _snprintf(text, sizeof(text) - 1, "%d x %d", w, h);
    text[sizeof(text) - 1] = '\0';

    SIZE sz = cv_text_extent(ts, 0, text);
    cv_label(c, w / 2 + 3, h / 2 + 3, ts, WHITE, BLACK, text);
    (void)sz;
}

static void pat_pixels(Canvas* c)
{
    static const char* labels[6] = {
        "1px checkerboard", "2px checkerboard", "1px vertical lines",
        "1px horizontal lines", "2px horizontal lines", "RGB vertical lines",
    };

    int w = c->w, h = c->h;
    int ts = small_text(c);

    for (int tile = 0; tile < 6; tile++)
    {
        int col = tile % 3, row = tile / 3;
        int x0 = w * col / 3, x1 = w * (col + 1) / 3;
        int y0 = h * row / 2, y1 = h * (row + 1) / 2;

        for (int y = y0; y < y1; y++)
        {
            for (int x = x0; x < x1; x++)
            {
                uint32_t p;

                switch (tile)
                {
                case 0: p = ((x + y) & 1) ? WHITE : BLACK; break;
                case 1: p = (((x >> 1) + (y >> 1)) & 1) ? WHITE : BLACK; break;
                case 2: p = (x & 1) ? WHITE : BLACK; break;
                case 3: p = (y & 1) ? WHITE : BLACK; break;
                case 4: p = ((y >> 1) & 1) ? WHITE : BLACK; break;
                default:
                    p = (x % 3 == 0) ? COL(255, 0, 0) : (x % 3 == 1) ? COL(0, 255, 0) : COL(0, 0, 255);
                    break;
                }

                c->px[y * w + x] = p;
            }
        }

        cv_label(c, x0 + 4, y0 + 3, ts, WHITE, BLACK, labels[tile]);
    }
}

static void pat_geometry(Canvas* c)
{
    int w = c->w, h = c->h;
    double par = cv_par(c);
    double cx = (w - 1) / 2.0, cy = (h - 1) / 2.0;
    int thick = h / 300 + 1;

    cv_fill(c, BLACK);

    cv_hline(c, 0, w - 1, h / 2, COL(80, 80, 80));
    cv_vline(c, w / 2, 0, h - 1, COL(80, 80, 80));

    /* If the pixels aren't square on the screen, also show what a circle
       looks like when drawn with square pixels */
    double r = h * 0.45;
    if (fabs(par - 1.0) > 0.01)
        cv_ellipse(c, cx, cy, r, r, COL(90, 90, 160), 1);

    /* Round on the screen */
    cv_ellipse(c, cx, cy, r / par, r, WHITE, thick);

    double half = r / sqrt(2.0);
    cv_outline(c, (int)lround(cx - half / par), (int)lround(cy - half),
               (int)lround(cx + half / par), (int)lround(cy + half), WHITE);

    double rc = h * 0.08;
    double m = 3.0 + thick;
    cv_ellipse(c, m + rc / par, m + rc, rc / par, rc, WHITE, thick);
    cv_ellipse(c, w - 1 - m - rc / par, m + rc, rc / par, rc, WHITE, thick);
    cv_ellipse(c, m + rc / par, h - 1 - m - rc, rc / par, rc, WHITE, thick);
    cv_ellipse(c, w - 1 - m - rc / par, h - 1 - m - rc, rc / par, rc, WHITE, thick);

    cv_outline(c, 0, 0, w - 1, h - 1, WHITE);

    int ts = small_text(c);
    char aspect[32], text[128];
    aspect_name(cv_screen_aspect(c), aspect, sizeof(aspect));
    if (fabs(par - 1.0) <= 0.01)
        _snprintf(text, sizeof(text) - 1, "Circles are round with square pixels");
    else
        _snprintf(text, sizeof(text) - 1, "Circles are round on a %s screen (pixel aspect %.3f)", aspect, par);
    text[sizeof(text) - 1] = '\0';

    SIZE sz = cv_text_extent(ts, 0, text);
    cv_label(c, (w - sz.cx) / 2, h - sz.cy - (int)m - 2, ts, WHITE, BLACK, text);
}

static void pat_dither(Canvas* c)
{
    static const uint32_t pairs[4][2] = {
        { COL(0, 0, 0), COL(255, 255, 255) },
        { COL(255, 0, 0), COL(0, 0, 255) },
        { COL(0, 0, 255), COL(255, 255, 0) },
        { COL(0, 255, 0), COL(255, 0, 255) },
    };
    static const int bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
    };

    int w = c->w, h = c->h;
    int ts = small_text(c);

    /* Top half: 50% checkerboard dithers, each above a solid strip of the colour it should blend to */
    for (int i = 0; i < 4; i++)
    {
        int x0 = w * i / 4, x1 = w * (i + 1) / 4;
        int ym = h / 4, y1 = h / 2;

        for (int y = 0; y < ym; y++)
            for (int x = x0; x < x1; x++)
                c->px[y * w + x] = pairs[i][(x + y) & 1];

        cv_rect(c, x0, ym, x1, y1, average(pairs[i][0], pairs[i][1]));
    }

    /* Bottom half: an ordered-dither gradient using only black and white, above a smooth gradient */
    for (int y = h / 2; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            uint32_t p;

            if (y < h * 3 / 4)
            {
                int level = w > 1 ? x * 17 / w : 0; /* 0..16 */
                p = (bayer[y & 3][x & 3] < level) ? WHITE : BLACK;
            }
            else
            {
                int v = w > 1 ? x * 255 / (w - 1) : 0;
                p = COL(v, v, v);
            }

            c->px[y * w + x] = p;
        }
    }

    cv_label(c, 4, 3, ts, WHITE, BLACK, "Checkerboard dithers / solid colour");
    cv_label(c, 4, h / 2 + 3, ts, WHITE, BLACK, "Ordered dither / smooth gradient");
}

static void pat_text(Canvas* c)
{
    static const int sizes[] = { 8, 10, 12, 14, 16, 20, 24, 32, 48 };
    static const struct { const char* text; uint32_t fg, bg; } colored[] = {
        { "White on blue", COL(255, 255, 255), COL(0, 0, 170) },
        { "Yellow on blue", COL(255, 255, 85), COL(0, 0, 170) },
        { "Red on blue", COL(255, 0, 0), COL(0, 0, 255) },
        { "Black on white", COL(0, 0, 0), COL(255, 255, 255) },
        { "Green on black", COL(0, 255, 0), COL(0, 0, 0) },
        { "White on red", COL(255, 255, 255), COL(200, 0, 0) },
    };

    int w = c->w, h = c->h;
    int y = 4;

    cv_fill(c, BLACK);

    for (int i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++)
    {
        int s = sizes[i];
        if (s > h / 8 || y + s > h / 2)
            break;

        char line[160];
        _snprintf(line, sizeof(line) - 1, "%dpx: The quick brown fox jumps over the lazy dog 0123456789", s);
        line[sizeof(line) - 1] = '\0';

        cv_text(c, 4, y, s, WHITE, line);
        y += s + s / 3 + 2;
    }

    /* Coloured text, wrapping onto new lines */
    int ts = cv_text_size(c);
    int x = 4;
    y += ts / 2;

    for (int i = 0; i < (int)(sizeof(colored) / sizeof(colored[0])); i++)
    {
        SIZE sz = cv_text_extent(ts, 0, colored[i].text);

        if (x + sz.cx + 4 > w && x > 4)
        {
            x = 4;
            y += sz.cy + 6;
        }

        cv_label(c, x + 2, y, ts, colored[i].fg, colored[i].bg, colored[i].text);
        x += sz.cx + 12;
    }

    /* 80 and 40 columns across the full width */
    const char* digits = "1234567890123456789012345678901234567890"
                         "1234567890123456789012345678901234567890";

    int cw80 = w / 80;
    int cw40 = w / 40;
    int y_bottom = h - 4;

    if (cw80 >= 3)
    {
        y_bottom -= cw80 * 2;
        cv_text_ex(c, 0, y_bottom, cw80 * 2, cw80, COL(255, 255, 255), digits);
        y_bottom -= 4;
    }

    if (cw40 >= 3)
    {
        char d40[41];
        memcpy(d40, digits, 40);
        d40[40] = '\0';

        y_bottom -= cw40 * 2;
        cv_text_ex(c, 0, y_bottom, cw40 * 2, cw40, COL(255, 255, 255), d40);
    }
}

static int triangle(int t, int range)
{
    if (range <= 0)
        return 0;

    int p = t % (2 * range);
    return p < range ? p : 2 * range - p;
}

static void pat_motion(Canvas* c, uint32_t frame)
{
    int w = c->w, h = c->h;
    double par = cv_par(c);

    cv_fill(c, COL(24, 24, 24));

    /* A wide bar and a 1px line sweeping across: tearing shows as a break in them */
    int bar_w = w / 40 > 2 ? w / 40 : 2;
    int speed = w / 120 > 1 ? w / 120 : 1;
    int bar_x = (int)((frame * (uint32_t)speed) % (uint32_t)(w + bar_w)) - bar_w;
    cv_rect(c, bar_x, 0, bar_x + bar_w, h, WHITE);

    int line_speed = w / 240 > 1 ? w / 240 : 1;
    int line_x = (int)((frame * (uint32_t)line_speed) % (uint32_t)w);
    cv_vline(c, line_x, 0, h - 1, COL(255, 255, 0));

    /* A bouncing square, square on the screen */
    int box_h = h / 8 > 2 ? h / 8 : 2;
    int box_w = (int)lround(box_h / par);
    int vx = w / 200 > 1 ? w / 200 : 1;
    int vy = h / 150 > 1 ? h / 150 : 1;
    int bx = triangle((int)(frame * (uint32_t)vx), w - box_w);
    int by = triangle((int)(frame * (uint32_t)vy), h - box_h);
    cv_rect(c, bx, by, bx + box_w, by + box_h, COL(0, 160, 255));

    int ts = cv_text_size(c) * 2;
    char text[32];
    _snprintf(text, sizeof(text) - 1, "%u", frame);
    text[sizeof(text) - 1] = '\0';

    SIZE sz = cv_text_extent(ts, 0, text);
    cv_label(c, (w - sz.cx) / 2, (h - sz.cy) / 2, ts, WHITE, BLACK, text);
}

static void pat_interlaced(Canvas* c)
{
    pat_color_bars(c);

    for (int y = 1; y < c->h; y += 2)
        memset(&c->px[y * c->w], 0, (size_t)c->w * sizeof(uint32_t));
}

static void pat_palette(Canvas* c, const PatternContext* ctx)
{
    int w = c->w, h = c->h;

    /* In 8-bit modes the app animates the palette itself; otherwise the
       cycling is drawn here so other bit depths show the same thing */
    c->use_idx = ctx->hw_palette;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int index;

            if (y < h / 2)
            {
                /* Bands of all 256 entries */
                index = x * 256 / w;
            }
            else
            {
                /* 16 x 16 swatches */
                index = ((y - h / 2) * 16 / (h - h / 2)) * 16 + x * 16 / w;
            }

            uint32_t color;

            if (ctx->hw_palette || index >= PAL_CYCLE_COUNT)
                color = ctx->palette[index];
            else
                color = ctx->palette[(index + ctx->frame) % PAL_CYCLE_COUNT];

            c->px[y * w + x] = color;

            if (c->use_idx)
                c->idx[y * w + x] = (uint8_t)index;
        }
    }

    cv_label(c, 4, 3, small_text(c), WHITE, BLACK, ctx->hw_palette ?
             "Palette cycling: SetEntries every frame (greys stay)" :
             "Palette cycling (drawn; not an 8-bit mode)");
}

/* 1:1, centred: a bigger image is cropped, a smaller one has a border */
static void pan_range(int iw, int w, int* pan)
{
    /* Centred, the picture starts (iw - w) / 2 pixels in; it can move as far
       as either edge, and not at all where it's smaller than the screen */
    int lo = 0, hi = 0;
    if (iw > w)
    {
        lo = -((iw - w) / 2);
        hi = (iw - w) + lo;
    }
    if (*pan < lo) *pan = lo;
    if (*pan > hi) *pan = hi;
}

void image_pan_clamp(int iw, int ih, int w, int h, int* pan_x, int* pan_y)
{
    pan_range(iw, w, pan_x);
    pan_range(ih, h, pan_y);
}

/* The image 1:1, centred and moved by the pan offset, cropped to the screen */
static void draw_centred(Canvas* c, const uint32_t* img, int iw, int ih, const PatternContext* ctx)
{
    int w = c->w, h = c->h;
    int pan_x = ctx->pan_x, pan_y = ctx->pan_y;
    image_pan_clamp(iw, ih, w, h, &pan_x, &pan_y);

    int ox = (w - iw) / 2 - pan_x, oy = (h - ih) / 2 - pan_y;

    cv_fill(c, COL(40, 40, 40));

    for (int y = 0; y < h; y++)
    {
        int sy = y - oy;
        if (sy < 0 || sy >= ih)
            continue;

        int x0 = ox > 0 ? ox : 0;
        int x1 = ox + iw < w ? ox + iw : w;
        if (x1 > x0)
            memcpy(&c->px[y * w + x0], &img[sy * iw + (x0 - ox)], (size_t)(x1 - x0) * sizeof(uint32_t));
    }
}

static void pat_test_image(Canvas* c, const PatternContext* ctx)
{
    int iw, ih;
    const uint32_t* img = testimage_get(&iw, &ih);

    if (!img)
    {
        cv_fill(c, BLACK);
        cv_label(c, 4, 4, cv_text_size(c), WHITE, BLACK, "Couldn't decode the built-in test image");
        return;
    }

    draw_centred(c, img, iw, ih, ctx);
}

static void pat_image(Canvas* c, const PatternContext* ctx)
{
    if (!ctx->image || ctx->image_w <= 0 || ctx->image_h <= 0)
    {
        cv_fill(c, COL(40, 40, 40));
        cv_label(c, 4, 4, cv_text_size(c), WHITE, BLACK, "No image file loaded - choose one in the panel");
        return;
    }

    draw_centred(c, ctx->image, ctx->image_w, ctx->image_h, ctx);
}

void pattern_draw(int id, Canvas* c, const PatternContext* ctx)
{
    c->use_idx = FALSE;

    switch (id)
    {
    case PAT_COLOR_BARS: pat_color_bars(c); break;
    case PAT_RAMPS: pat_ramps(c); break;
    case PAT_GRID: pat_grid(c); break;
    case PAT_PIXELS: pat_pixels(c); break;
    case PAT_GEOMETRY: pat_geometry(c); break;
    case PAT_DITHER: pat_dither(c); break;
    case PAT_TEXT: pat_text(c); break;
    case PAT_MOTION: pat_motion(c, ctx->frame); break;
    case PAT_INTERLACED: pat_interlaced(c); break;
    case PAT_PALETTE: pat_palette(c, ctx); break;
    case PAT_WHITE:
        cv_fill(c, WHITE);
        cv_text(c, 4, c->h - small_text(c) - 4, small_text(c), COL(128, 128, 128), "100% white");
        break;
    case PAT_TEST_IMAGE: pat_test_image(c, ctx); break;
    case PAT_IMAGE: pat_image(c, ctx); break;
    default: cv_fill(c, BLACK); break;
    }
}
