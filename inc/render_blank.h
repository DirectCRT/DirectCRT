#ifndef RENDER_BLANK_H
#define RENDER_BLANK_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* For when neither Direct3D 11 nor OpenGL can start once the game is
 * running: a black window with a message. (When neither works from the
 * start, DirectCRT steps aside for Windows' own ddraw.dll: dd_step_aside) */

DWORD WINAPI blank_render_main(void);

#endif
