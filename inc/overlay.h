#ifndef OVERLAY_H
#define OVERLAY_H

/* The overlay: the status OSD drawn over the finished frame, after the
 * screenshot is taken and before it's presented.
 *
 * src/overlay_status.c (C) keeps the hotkey's state and the frame timings and
 * puts the status into words; src/overlay.cpp (C++) draws it with Dear ImGui,
 * on Direct3D 11 or OpenGL. Everything but overlay_toggle_status runs on
 * the render thread. */

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

#define OVERLAY_MAX_ROWS 16
#define OVERLAY_HISTORY 240

typedef enum OVERLAY_CORNER
{
    OVERLAY_TOP_LEFT = 0,
    OVERLAY_TOP_RIGHT,
    OVERLAY_BOTTOM_LEFT,
    OVERLAY_BOTTOM_RIGHT
} OVERLAY_CORNER;

/* The status OSD as text: rows of a label and a value (basic rows have no
 * label), and the frame times for the graph */
typedef struct OVERLAY_STATUS
{
    OVERLAY_CORNER corner;
    float font_size;          /* in pixels */
    float scale;              /* for spacing: font_size / 13 */

    int rows;
    struct
    {
        char label[24];
        char value[128];
    } row[OVERLAY_MAX_ROWS];

    BOOL graph;               /* status_osd_detail=full */
    float frame_ms[OVERLAY_HISTORY]; /* oldest first */
    int frame_count;
    float graph_max_ms;
} OVERLAY_STATUS;

/* overlay_status.c */

void overlay_toggle_status(void); /* the hotkey: show or hide, any thread */
BOOL overlay_status_visible(void);

/* How long the render thread may wait for the game: while the OSD is shown it
 * wakes up a few times a second, so the numbers keep moving */
DWORD overlay_wait_timeout(DWORD timeout);

/* Once per presented frame; new_frame = the game's picture had changed */
void overlay_frame_presented(BOOL new_frame);

/* The crt-auto passes' GPU time of one frame, when the renderer measured it */
void overlay_gpu_time(double ms);

/* The status for a canvas (the window's client area or the screen) */
void overlay_get_status(OVERLAY_STATUS* status, int canvas_width, int canvas_height);

/* The display's scale (1 = 96 dpi) */
float overlay_dpi_scale(void);

/* overlay_menu.c: the settings OSD. While it's open the game keeps running,
 * but its keyboard and mouse input goes to the OSD: the game's window
 * procedure hands it over (overlay_menu_wndproc) and the render thread takes
 * it (overlay_menu_events). The OSD writes directcrt.ini itself, and the game's
 * thread applies the settings that apply at once */

typedef enum OVERLAY_EVENT_TYPE
{
    OVERLAY_EVENT_MOUSE,  /* x, y */
    OVERLAY_EVENT_BUTTON, /* button, down */
    OVERLAY_EVENT_WHEEL,  /* x, y in notches */
    OVERLAY_EVENT_KEY,    /* vk, down, ctrl, shift, alt */
    OVERLAY_EVENT_CHAR    /* ch */
} OVERLAY_EVENT_TYPE;

typedef struct OVERLAY_EVENT
{
    OVERLAY_EVENT_TYPE type;
    float x, y;           /* MOUSE: in the client area, 0 to 1 */
    int button;           /* 0 left, 1 right, 2 middle, 3 and 4 the side buttons */
    BOOL down;
    int vk;               /* left and right shift, ctrl and alt told apart */
    BOOL ctrl, shift, alt;
    WCHAR ch;
} OVERLAY_EVENT;

/* The game's thread */
void overlay_toggle_menu(void);
void overlay_close_menu(void);
BOOL overlay_menu_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result); /* TRUE = taken */
BOOL overlay_menu_capturing(void); /* a hotkey is being set: the hotkeys are off */

/* Any thread */
BOOL overlay_menu_open(void);
BOOL overlay_visible(void); /* the status OSD or the settings OSD */

/* The render thread */
int overlay_menu_events(OVERLAY_EVENT* events, int max);
void overlay_menu_set_capturing(BOOL capturing);
void overlay_menu_request_close(void);
float overlay_menu_font_size(int canvas_height);

/* directcrt.ini's values, by settings_get's index. game = the game's own
 * section; *set = the section has the key (else the value is the default, or
 * for the game's section the [directcrt] value) */
const char* overlay_menu_value(int setting, BOOL game, BOOL* set);
void overlay_menu_write(int setting, BOOL game, const char* value); /* NULL = remove the key */
const char* overlay_menu_game(void); /* the game's section name */

/* overlay.cpp: draw the OSD if it's shown. FALSE = it couldn't be drawn */

BOOL overlay_d3d11_draw(
    struct ID3D11Device* device,
    struct ID3D11DeviceContext* ctx,
    struct ID3D11RenderTargetView* rtv,
    int width,
    int height);
void overlay_d3d11_release(void);

/* Into the current context's default framebuffer, width x height; the canvas
 * is its top canvas_height lines (the nonexclusive window is a line taller) */
BOOL overlay_ogl_draw(int width, int height, int canvas_height, const char* glsl_version);
void overlay_ogl_release(void);

#ifdef __cplusplus
}
#endif

#endif
