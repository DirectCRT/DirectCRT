#ifndef DISPLAY_SETTINGS_H
#define DISPLAY_SETTINGS_H

#include "crt_auto.h"

/*
 * The display settings: display, window_size, window_position and
 * remember_window, and the translation of cnc-ddraw's old keys.
 *
 * Pure C without Windows headers, so tests/test_display_settings.c can test it.
 * The window_size and window_position parsing follows DOSBox Staging's
 * src/gui/window_geometry.cpp (GPL-2.0-or-later).
 */

typedef enum DISPLAY_MODE
{
    DISPLAY_AUTO = 0,
    DISPLAY_WINDOW,
    DISPLAY_FULLSCREEN,
    DISPLAY_ORIGINAL,
} DISPLAY_MODE;

typedef enum GEOMETRY_UNIT
{
    GEOMETRY_LOGICAL = 0,   /* pixels at 100% scaling, as Windows' display scale */
    GEOMETRY_PIXELS,
    GEOMETRY_PERCENT,       /* sizes: of the desktop height; positions: of its width and height */
} GEOMETRY_UNIT;

typedef struct WINDOW_SIZE
{
    int unscaled;           /* the game's own resolution at 1:1 */
    float w;
    float h;
    GEOMETRY_UNIT unit;
} WINDOW_SIZE;

typedef struct WINDOW_POSITION
{
    int is_auto;            /* centred on the game's own window or on the screen */
    float x;
    float y;
    GEOMETRY_UNIT unit;
} WINDOW_POSITION;

typedef enum REMEMBER_WINDOW
{
    REMEMBER_WINDOW_OFF = 0,
    REMEMBER_WINDOW_ON,
} REMEMBER_WINDOW;

/* How a window or a fullscreen picture is shown, as cnc-ddraw's
 * windowed/fullscreen pair */
typedef enum DISPLAY_KIND
{
    DISPLAY_KIND_WINDOW = 0,        /* windowed=1, fullscreen=0 */
    DISPLAY_KIND_DESKTOP,           /* windowed=0, fullscreen=1: fullscreen at the desktop resolution */
    DISPLAY_KIND_MODE_SWITCH,       /* windowed=0, fullscreen=0: the monitor switches to the game's resolution */
} DISPLAY_KIND;

#define WINDOW_MIN_WIDTH 320        /* logical units, as DOSBox */
#define WINDOW_MIN_HEIGHT 200

/* The parse functions return 1 and fill *out on success, and return 0 and
 * leave *out alone for an invalid value. Values are case-insensitive. */
int ds_parse_display(const char* value, DISPLAY_MODE* out);
int ds_parse_window_size(const char* value, WINDOW_SIZE* out);
int ds_parse_window_position(const char* value, WINDOW_POSITION* out);
int ds_parse_remember_window(const char* value, REMEMBER_WINDOW* out);

/* on/off, true/false, yes/no, 1/0 */
int ds_parse_bool(const char* value, int* out);

const char* ds_display_name(DISPLAY_MODE mode);
const char* ds_remember_window_name(REMEMBER_WINDOW value);

/* How the game starts: game_fullscreen is whether it asked for exclusive
 * fullscreen (DDSCL_FULLSCREEN), as opposed to drawing in its own window */
DISPLAY_KIND ds_display_start(DISPLAY_MODE mode, int game_fullscreen);

/* Which fullscreen Alt+Enter goes to from a window */
DISPLAY_KIND ds_display_fullscreen_kind(DISPLAY_MODE mode);

/* The window's client size in pixels, on display: the desktop in pixels and
 * Windows' display scale (1.5 for 150%). game_* is the game's resolution, for
 * 'unscaled', which is not limited to the minimum size. */
void ds_window_client_size(
    const WINDOW_SIZE* size,
    const CRT_AUTO_DISPLAY* display,
    int game_width,
    int game_height,
    int* out_width,
    int* out_height);

/* A position that isn't 'auto', in pixels from the desktop's top left */
void ds_window_position_pixels(
    const WINDOW_POSITION* position,
    const CRT_AUTO_DISPLAY* display,
    int* out_x,
    int* out_y);

/* Formatting for remember_window, into buf (at least 32 bytes) */
void ds_format_size_pixels(char* buf, int buf_size, int width, int height);
void ds_format_position_pixels(char* buf, int buf_size, int x, int y);

/*
 * Old keys. Each returns the new value, or NULL when there's nothing to
 * translate. Empty strings count as not set.
 */

/* windowed and fullscreen (either may be NULL or empty) -> display */
const char* ds_legacy_display(const char* windowed, const char* fullscreen);

/* width and height -> window_size=WxHpx, into buf; 0 if not set or 0 */
int ds_legacy_window_size(const char* width, const char* height, char* buf, int buf_size);

/* posX and posY -> window_position=X,Ypx or auto (-32000), into buf */
int ds_legacy_window_position(const char* pos_x, const char* pos_y, char* buf, int buf_size);

/* savesettings=0/1/2 -> remember_window (off, on, on) */
const char* ds_legacy_remember_window(const char* savesettings);

/* cnc-ddraw's shader names and paths -> shader. A new value is returned
 * unchanged (as its canonical name); anything else is crt-auto. */
const char* ds_legacy_shader(const char* shader);

/* renderer: openglcore, direct3d9, direct3d9on12, gdi and software -> auto; others unchanged
 * (NULL when the value isn't an old one) */
const char* ds_legacy_renderer(const char* renderer);

#endif
