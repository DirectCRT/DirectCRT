#ifndef DDTEST_H
#define DDTEST_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

/* 0x00RRGGBB */
#define COL(r, g, b) ((uint32_t)((((r) & 255) << 16) | (((g) & 255) << 8) | ((b) & 255)))
#define COL_R(c) (((c) >> 16) & 255)
#define COL_G(c) (((c) >> 8) & 255)
#define COL_B(c) ((c) & 255)
#define WHITE COL(255, 255, 255)
#define BLACK COL(0, 0, 0)

/* The image the app draws, before it's converted to the surface's pixel format */
typedef struct Canvas
{
    int w, h;
    uint32_t* px;  /* w * h pixels, 0x00RRGGBB */
    uint8_t* idx;  /* w * h palette indices, used instead of px in 8-bit modes when use_idx is set */
    BOOL use_idx;
} Canvas;

enum
{
    PAT_COLOR_BARS,
    PAT_RAMPS,
    PAT_GRID,
    PAT_PIXELS,
    PAT_GEOMETRY,
    PAT_DITHER,
    PAT_TEXT,
    PAT_MOTION,
    PAT_INTERLACED,
    PAT_PALETTE,
    PAT_WHITE,
    PAT_IMAGE,
    PAT_COUNT
};

typedef struct PatternContext
{
    uint32_t frame;
    const uint32_t* palette;  /* the 256-entry base palette */
    BOOL hw_palette;          /* 8-bit mode: palette cycling is done by the app with SetEntries */
    const uint32_t* image;    /* loaded image file, may be NULL */
    int image_w, image_h;
    BOOL image_fit;
} PatternContext;

/* patterns.c */
const char* pattern_name(int id);
const char* pattern_short_name(int id);
int pattern_from_short_name(const char* name);
void pattern_draw(int id, Canvas* c, const PatternContext* ctx);

/* 8-bit palette: a 6x6x6 colour cube (indices 0-215) plus 40 greys (216-255) */
#define PAL_CYCLE_COUNT 216
void palette_build(uint32_t pal[256]);
void palette_build_lut(const uint32_t pal[256]);
uint8_t palette_lookup(uint32_t color);

/* Drawing helpers; coordinates are clipped to the canvas */
void cv_put(Canvas* c, int x, int y, uint32_t color);
void cv_rect(Canvas* c, int x0, int y0, int x1, int y1, uint32_t color); /* filled, x1/y1 exclusive */
void cv_fill(Canvas* c, uint32_t color);
void cv_hline(Canvas* c, int x0, int x1, int y, uint32_t color);           /* inclusive */
void cv_vline(Canvas* c, int x, int y0, int y1, uint32_t color);           /* inclusive */
void cv_outline(Canvas* c, int x0, int y0, int x1, int y1, uint32_t color); /* inclusive */
void cv_ellipse(Canvas* c, double cx, double cy, double rx, double ry, uint32_t color, int thickness);
double cv_par(const Canvas* c);      /* pixel aspect ratio (width/height) on a 4:3 screen */
int cv_text_size(const Canvas* c);   /* a text height that suits the canvas size */
int cv_text(Canvas* c, int x, int y, int height, uint32_t color, const char* s); /* returns width */
int cv_text_ex(Canvas* c, int x, int y, int height, int char_width, uint32_t color, const char* s);
SIZE cv_text_extent(int height, int char_width, const char* s);
int cv_label(Canvas* c, int x, int y, int height, uint32_t fg, uint32_t bg, const char* s); /* returns height used */
void cv_shutdown(void);

/* image.c */
BOOL image_load(const char* path, uint32_t** px, int* w, int* h, char* err, int err_len);
BOOL image_save_png(const char* path, const uint32_t* px, int w, int h, char* err, int err_len);

#endif
