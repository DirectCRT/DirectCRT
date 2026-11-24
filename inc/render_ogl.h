#ifndef RENDER_OGL_H
#define RENDER_OGL_H

#include <windows.h>
#include "opengl_utils.h"
#include "crt_auto.h"

#define TEXTURE_COUNT 4
#define FBO_COUNT 2

#ifdef _DEBUG
#define GL_CHECK(stmt) do { \
            stmt; \
            ogl_check_error(#stmt); \
        } while (0)
#else
#define GL_CHECK(stmt) stmt
#endif

typedef struct OGLRENDERER
{
    HWND hwnd;
    HDC hdc;
    HGLRC context;
    GLuint main_program;
    GLuint shader1_program;
    GLuint shader2_program;
    BOOL got_error;
    int surface_tex_width;
    int surface_tex_height;
    GLenum surface_format;
    GLenum surface_type;
    GLuint surface_tex_ids[TEXTURE_COUNT];
    GLuint palette_tex_ids[TEXTURE_COUNT];
    float scale_w;
    float scale_h;
    GLint main_tex_coord_attr_loc;
    GLint main_vertex_coord_attr_loc;
    GLuint main_vbos[3];
    GLuint main_vao;
    GLuint frame_buffer_id[FBO_COUNT];
    GLuint frame_buffer_tex_id[FBO_COUNT];
    GLuint shader1_vbos[2];
    GLuint shader2_vbos[2];
    GLuint shader1_vao;
    GLuint shader2_vao;
    BOOL use_opengl;

    /* shader1 is DOSBox Staging's image-adjustments pass at the size of the
     * doubled image, shader2 the shader crt-auto chose for the window */
    BOOL crt_auto;
    CRT_AUTO_PARAMS crt_params;
    GLuint gpu_time_query;
    BOOL gpu_time_pending;
    double gpu_time_ms;
    int gpu_time_frames;
} OGLRENDERER;

DWORD WINAPI ogl_render_main(void);
BOOL ogl_probe(); /* whether an OpenGL 3 context can be created */
BOOL ogl_create();
BOOL ogl_release();

#endif
