#ifndef RENDER_D3D11_H
#define RENDER_D3D11_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Direct3D 11 renderer (feature level 10.0+, flip-model swap chain, no
 * exclusive fullscreen). With crt-auto it runs DOSBox Staging's shaders,
 * translated at build time (tools/shaders/gen_d3d11_shaders.py); otherwise
 * it scales the game's picture to the viewport.
 *
 * The device and swap chain are created on the game's thread and kept while
 * the window stays the same; the textures and shaders for the current mode
 * are created by the render thread. */

BOOL d3d11_is_available();
BOOL d3d11_create();
DWORD WINAPI d3d11_render_main(void);
BOOL d3d11_release();

#endif
