/* The settings OSD's ImGui side (overlay_menu_ui.cpp, overlay_viewport.cpp);
 * C++ only. overlay_menu.c keeps its state and its queue of input */

#ifndef OVERLAY_MENU_UI_H
#define OVERLAY_MENU_UI_H

extern "C" {
#include "settings.h"
}

/* The frame's keyboard and mouse, from overlay_menu.c's queue */
void menu_input(int width, int canvas_height);

/* The OSD at the font size F; first = it wasn't in the last frame */
void menu_draw(int width, int canvas_height, float F, bool first);

/* It closed: what was open, typed or held is forgotten */
void menu_reset(void);

/* The viewport setting's editor */
void menu_viewport(int setting, const SETTING* s, float F);

/* The viewport_offset setting's editor, and the picture being moved with
 * the keys and the mouse, which it starts; overlay.h's OVERLAY_EVENT */
void menu_viewport_offset(int setting, const SETTING* s, float F);
bool menu_pan_active(void);
void menu_pan_key(const OVERLAY_EVENT* e);
void menu_pan_draw(int width, int canvas_height, float F);
void menu_pan_end(bool keep);

/* A key and what it does, drawn right to left from x (measured only when dl
 * is NULL); returns where the next one goes, or -1 when it didn't fit after min_x */
float menu_hint(ImDrawList* dl, float x, float cy, float min_x, const char* key, const char* what, float F);

#endif
