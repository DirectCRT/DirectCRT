#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ddsurface.h"

/* The screenshot key: asks the render thread for the next frame */
void ss_request();

/* The render thread, once a frame. When ss_begin returns TRUE, save the game's
   surface with ss_save_raw (holding g_ddraw.cs) and the finished frame, before
   it's shown, with ss_save_shaded. Both copy what they're given and encode the
   PNG on another thread. */
BOOL ss_begin();
void ss_save_raw(struct IDirectDrawSurfaceImpl* src);
void ss_save_shaded(const void* pixels, int width, int height, int pitch, BOOL bgra, BOOL bottom_up);

#endif
