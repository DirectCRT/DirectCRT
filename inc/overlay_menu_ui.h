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

#endif
