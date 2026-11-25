/* The OSDs' look and their controls, on Dear ImGui (overlay_widgets.cpp):
 * the palette, text in the OSDs' fonts, the motion, and the widgets the
 * settings OSD is made of. C++ only: overlay.cpp and the settings OSD's files */

#ifndef OVERLAY_WIDGETS_H
#define OVERLAY_WIDGETS_H

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_internal.h"

/* A buffer for a line of the OSDs' text */
#define MENU_TEXT 264

/* The fonts (overlay.cpp loads them) */
extern ImFont* g_font;  /* Source Sans 3 */
extern ImFont* g_icons; /* Material Icons, cut down (inc/overlay_icons.h); NULL = no icons */

/* Dark, with one blue for what's on and what has the focus */
static const ImVec4 COL_PANEL(0.086f, 0.090f, 0.102f, 0.97f);
static const ImVec4 COL_BAR(0.059f, 0.063f, 0.071f, 1.0f);
static const ImVec4 COL_DIALOG(0.114f, 0.119f, 0.135f, 0.99f);
static const ImVec4 COL_ROW_HOVER(0.165f, 0.173f, 0.192f, 1.0f);
static const ImVec4 COL_ROW_ACTIVE(0.200f, 0.210f, 0.235f, 1.0f);
static const ImVec4 COL_PILL(0.137f, 0.145f, 0.165f, 1.0f);
static const ImVec4 COL_GROUP(0.128f, 0.140f, 0.172f, 1.0f);
static const ImVec4 COL_PILL_HOVER(0.190f, 0.200f, 0.225f, 1.0f);
static const ImVec4 COL_TEXT(0.925f, 0.925f, 0.930f, 1.0f);
static const ImVec4 COL_DIM(0.600f, 0.610f, 0.640f, 1.0f);
static const ImVec4 COL_HEADING(0.650f, 0.660f, 0.690f, 1.0f);
static const ImVec4 COL_RULE(0.240f, 0.250f, 0.270f, 1.0f);
static const ImVec4 COL_ACCENT(0.300f, 0.560f, 0.900f, 1.0f);
static const ImVec4 COL_ACCENT_HI(0.480f, 0.690f, 0.960f, 1.0f);
static const ImVec4 COL_TRACK(0.290f, 0.300f, 0.320f, 1.0f);
static const ImVec4 COL_KNOB(0.880f, 0.880f, 0.890f, 1.0f);

/* A colour of the palette; GetColorU32 fades it with the style's alpha (greyed out items) */
ImU32 ui_col(const ImVec4& c, float alpha = 1.0f);

/* Text in Source Sans 3 at a size, on a draw list, and its size */
ImVec2 ui_text_size(float size, const char* text, float wrap = 0.0f);
void ui_text(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* text, float wrap = 0.0f);

/* text, cut with an ellipsis to fit max_w (into buf) */
const char* ui_fit(float size, const char* text, float max_w, char* buf, size_t buf_size);

/* An icon (overlay_icons.h) centred on center */
void ui_icon(ImDrawList* dl, const char* icon, ImVec2 center, float size, ImU32 col);

/* A switch, on or off, at pos */
void ui_switch(ImDrawList* dl, ImVec2 pos, float w, float h, bool on, bool hovered);

/* The motion. g_ui_moving: something moved this frame, and a change waits
 * until nothing does */
extern bool g_ui_moving;
float ui_dt(void);
float ui_approach(float x, float target, float speed, float snap, float min_rate = 0.0f);
ImRect ui_slide(ImGuiID id, const ImRect& target, ImVec2 origin, bool snap);
ImRect ui_slide_hold(ImGuiID id, ImVec2 origin);
void ui_chevron(ImDrawList* dl, ImVec2 c, float size, float angle, ImU32 col);

/* The keyboard is in use: its cursor shows, and the mouse hasn't moved since */
bool ui_keyboard(void);

/* g_ui_take_focus: the next control to call ui_take_focus (straight after
 * its ItemAdd) gets the keyboard; set when a row is opened with it */
extern bool g_ui_take_focus;
void ui_take_focus(ImGuiID id);

/* The controls */
int ui_segmented(const char* str_id, const char* const* labels, int n, int selected, float F);
bool ui_button(const char* label, float w, bool accent);
void ui_note(const char* text, float F, const ImVec4& col = COL_DIM);
bool ui_int_field(const char* str_id, int v, int lo, int hi, float w, int* out);
bool ui_slider_box(const char* str_id, const char* label, int v, int lo, int hi, float F, int* out,
                   int box_hi = 0, int* live = NULL);

#endif
