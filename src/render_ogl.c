#include <windows.h>
#include <stdio.h>
#include "config.h"
#include "fps_limiter.h"
#include "opengl_utils.h"
#include "dd.h"
#include "ddsurface.h"
#include "openglshader.h"
#include "render_ogl.h"
#include "utils.h"
#include "debug.h"
#include "dllmain.h"
#include "hook.h"
#include "screenshot.h"
#include "overlay.h"


static HGLRC ogl_create_core_context(HDC hdc);
static void ogl_build_programs();
static void ogl_create_textures(int width, int height);
static void ogl_init_main_program();
static void ogl_init_shader1_program();
static void ogl_init_shader2_program();
static void ogl_build_crt_auto_programs(BOOL core_profile);
static void ogl_init_crt_auto_uniforms();
static void ogl_render();
static BOOL ogl_release_resources();
static BOOL ogl_texture_upload_test();
static BOOL ogl_shader_test();
static void ogl_check_error(const char* stmt);
static void ogl_free();

static OGLRENDERER g_ogl;

/* A window's pixel format can be set only once */
static void ogl_set_pixel_format(HDC hdc, BYTE bits)
{
    if (!hdc || GetPixelFormat(hdc))
        return;

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);

    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = bits;
    pfd.iLayerType = PFD_MAIN_PLANE;

    SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd);
}

/* Whether OpenGL 3 works here: a context on a hidden window of its own */
BOOL ogl_probe()
{
    if (!oglu_load_dll())
    {
        TRACE("     OpenGL: not available\n");
        return FALSE;
    }

    BOOL ok = FALSE;
    HWND hwnd = real_CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    HDC hdc = hwnd ? GetDC(hwnd) : NULL;

    if (hdc)
    {
        ogl_set_pixel_format(hdc, 32);

        HGLRC context = xwglCreateContext(hdc);

        if (context)
        {
            if (xwglMakeCurrent(hdc, context))
            {
                oglu_init();
                ok = g_oglu_got_version3;

                xwglMakeCurrent(NULL, NULL);
            }

            xwglDeleteContext(context);
        }

        ReleaseDC(hwnd, hdc);
    }

    if (hwnd)
        real_DestroyWindow(hwnd);

    TRACE("     OpenGL: %s (%s)\n", ok ? "available" : "not usable", g_oglu_version);

    return ok;
}

BOOL ogl_create()
{
    if (g_ogl.hwnd == g_ddraw.hwnd && g_ogl.hdc == g_ddraw.render.hdc && g_ogl.context)
    {
        return TRUE;
    }

    ogl_release();

    if (!oglu_load_dll())
        return FALSE;

    ogl_set_pixel_format(g_ddraw.render.hdc, (BYTE)g_ddraw.mode.dmBitsPerPel);

    g_ogl.context = xwglCreateContext(g_ddraw.render.hdc);
    if (g_ogl.context)
    {
        g_ogl.hwnd = g_ddraw.hwnd;
        g_ogl.hdc = g_ddraw.render.hdc;

        GLenum err = GL_NO_ERROR;
        BOOL made_current = FALSE;

        for (int i = 0; i < 5; i++)
        {
            if ((made_current = xwglMakeCurrent(g_ogl.hdc, g_ogl.context)))
                break;

            Sleep(50);
        }

        if (made_current && (err = glGetError()) == GL_NO_ERROR)
        {
            GL_CHECK(oglu_init());

            TRACE("+--OpenGL-----------------------------------------\n");
            TRACE("| GL_VERSION:                  %s\n", glGetString(GL_VERSION));
            TRACE("| GL_VENDOR:                   %s\n", glGetString(GL_VENDOR));
            TRACE("| GL_RENDERER:                 %s\n", glGetString(GL_RENDERER));
            TRACE("| GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
            TRACE("+------------------------------------------------\n");

#ifdef _DEBUG
            while (glGetError() != GL_NO_ERROR); /* Ignore errors from glGetString */
#endif

            GL_CHECK(g_ogl.context = ogl_create_core_context(g_ogl.hdc));
        }
        else
        {
            TRACE("OpenGL error %08x, GetLastError %lu (xwglMakeCurrent())\n", err, GetLastError());
            ogl_check_error("xwglMakeCurrent()");
        }

        for (int i = 0; i < 5; i++)
        {
            if (xwglMakeCurrent(NULL, NULL))
                break;

            Sleep(50);
        }

        return TRUE;
    }

    g_ogl.hwnd = NULL;
    g_ogl.hdc = NULL;

    return FALSE;
}

DWORD WINAPI ogl_render_main(void)
{
    Sleep(250);
    g_ogl.got_error = g_ogl.use_opengl = FALSE;
    GLenum err = GL_NO_ERROR;
    BOOL made_current = FALSE;

    for (int i = 0; i < 5; i++)
    {
        if ((made_current = xwglMakeCurrent(g_ogl.hdc, g_ogl.context)))
            break;

        Sleep(50);
    }

    if (made_current && (err = glGetError()) == GL_NO_ERROR)
    {
        GL_CHECK(oglu_init());

        g_ogl.got_error = g_ogl.got_error || (err = glGetError()) != GL_NO_ERROR;

        BOOL got_swap_ctrl;
        GL_CHECK(got_swap_ctrl = oglu_ext_exists("WGL_EXT_swap_control", g_ogl.hdc));

        if (got_swap_ctrl && wglSwapIntervalEXT)
            wglSwapIntervalEXT(g_config.vsync ? 1 : 0);

        fpsl_init();
        GL_CHECK(ogl_build_programs());
        GL_CHECK(ogl_create_textures(g_ddraw.width, g_ddraw.height));
        GL_CHECK(ogl_init_main_program());
        GL_CHECK(ogl_init_shader1_program());
        GL_CHECK(ogl_init_shader2_program());
        GL_CHECK(ogl_init_crt_auto_uniforms());

        g_ogl.got_error = g_ogl.got_error || (err = glGetError()) != GL_NO_ERROR;
        GL_CHECK(g_ogl.got_error = g_ogl.got_error || !ogl_texture_upload_test());
        GL_CHECK(g_ogl.got_error = g_ogl.got_error || !ogl_shader_test());
        g_ogl.got_error = g_ogl.got_error || (err = glGetError()) != GL_NO_ERROR;

        /* The shader pipeline or nothing: Direct3D 11 then */
        g_ogl.use_opengl = g_ogl.crt_auto && !g_ogl.got_error;

        GL_CHECK(ogl_render());

        GL_CHECK(ogl_release_resources());

        while (glGetError() != GL_NO_ERROR);
    }
    else
    {
        TRACE("OpenGL error %08x, GetLastError %lu (xwglMakeCurrent())\n", err, GetLastError());
        ogl_check_error("xwglMakeCurrent()");
    }

    for (int i = 0; i < 5; i++)
    {
        if (xwglMakeCurrent(NULL, NULL))
            break;

        Sleep(50);
    }

    if (!g_ogl.use_opengl)
    {
        TRACE("OpenGL: setup failed\n");
        ogl_free();
        dd_renderer_failed();
    }

    return 0;
}


static void ogl_check_error(const char* stmt)
{
#ifdef _DEBUG
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
    {
        g_ogl.got_error = TRUE;
        TRACE("OpenGL error %08x (%s)\n", err, stmt);
    }
#endif
}

static HGLRC ogl_create_core_context(HDC hdc)
{
    if (!wglCreateContextAttribsARB)
        return g_ogl.context;

    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3, /* DOSBox Staging's shaders are GLSL 3.30 */
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0 };

    HGLRC context = wglCreateContextAttribsARB(hdc, 0, attribs);
    BOOL made_current = context && xwglMakeCurrent(hdc, context);

    if (made_current)
    {
        xwglDeleteContext(g_ogl.context);
        oglu_init();

        TRACE("+--OpenGL Core-----------------------------------\n");
        TRACE("| GL_VERSION:                  %s\n", glGetString(GL_VERSION));
        TRACE("| GL_VENDOR:                   %s\n", glGetString(GL_VENDOR));
        TRACE("| GL_RENDERER:                 %s\n", glGetString(GL_RENDERER));
        TRACE("| GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
        TRACE("+------------------------------------------------\n");

        return context;
    }
    else if (context)
    {
        xwglDeleteContext(context);
    }

    return g_ogl.context;
}

static void ogl_build_programs()
{
    g_ogl.main_program = g_ogl.shader1_program = g_ogl.shader2_program = 0;
    g_ogl.crt_auto = FALSE;

    if (!g_oglu_got_version3)
        return;

    BOOL core_profile = wglCreateContextAttribsARB != NULL;

    /* main: the game's surface converted to RGB. shader1 and shader2: the
     * image adjustments and the shader (ogl_build_crt_auto_programs) */
    if (g_ddraw.bpp == 8)
    {
        g_ogl.main_program = oglu_build_program(PASSTHROUGH_VERT_SHADER, PALETTE_FRAG_SHADER, core_profile);
    }
    else if (g_ddraw.bpp == 16 && g_config.rgb555)
    {
        g_ogl.main_program = oglu_build_program(PASSTHROUGH_VERT_SHADER, RGB555_FRAG_SHADER, core_profile);
    }
    else if (g_ddraw.bpp == 16 || g_ddraw.bpp == 24 || g_ddraw.bpp == 32)
    {
        g_ogl.main_program = oglu_build_program(PASSTHROUGH_VERT_SHADER, PASSTHROUGH_FRAG_SHADER, core_profile);
    }

    if (g_ogl.main_program)
        ogl_build_crt_auto_programs(core_profile);
}

static void ogl_create_textures(int width, int height)
{
    GLenum err = GL_NO_ERROR;

    int w = width;
    int h = height;

    g_ogl.surface_tex_width =
        w <= 1024 ? 1024 : w <= 2048 ? 2048 : w <= 4096 ? 4096 : w <= 8192 ? 8192 : w;

    g_ogl.surface_tex_height =
        h <= 512 ? 512 : h <= 1024 ? 1024 : h <= 2048 ? 2048 : h <= 4096 ? 4096 : h <= 8192 ? 8192 : h;

    g_ogl.scale_w = (float)width / g_ogl.surface_tex_width;
    g_ogl.scale_h = (float)height / g_ogl.surface_tex_height;

    glGenTextures(TEXTURE_COUNT, g_ogl.surface_tex_ids);

    for (int i = 0; i < TEXTURE_COUNT; i++)
    {
        glBindTexture(GL_TEXTURE_2D, g_ogl.surface_tex_ids[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        g_ogl.got_error = g_ogl.got_error || (err = glGetError()) != GL_NO_ERROR;

        if (err != GL_NO_ERROR)
        {
            TRACE("OpenGL error %08x (ogl_create_textures())\n", err);
            ogl_check_error("ogl_create_textures()");
        }

        while (glGetError() != GL_NO_ERROR);

        if (g_ddraw.bpp == 32)
        {
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                g_ogl.surface_tex_width,
                g_ogl.surface_tex_height,
                0,
                g_ogl.surface_format = GL_BGRA,
                g_ogl.surface_type = GL_UNSIGNED_BYTE,
                0);
        }
        else if (g_ddraw.bpp == 24)
        {
            /* Rows of 24-bit surfaces are padded to 8 bytes, which GL's default
               unpack alignment of 4 gives with the row length ogl_render sets */
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                g_ogl.surface_tex_width,
                g_ogl.surface_tex_height,
                0,
                g_ogl.surface_format = GL_BGR,
                g_ogl.surface_type = GL_UNSIGNED_BYTE,
                0);
        }
        else if (g_ddraw.bpp == 16 && g_config.rgb555)
        {
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RG8,
                g_ogl.surface_tex_width,
                g_ogl.surface_tex_height,
                0,
                g_ogl.surface_format = GL_RG,
                g_ogl.surface_type = GL_UNSIGNED_BYTE,
                0);
        }
        else if (g_ddraw.bpp == 16)
        {
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGB565,
                g_ogl.surface_tex_width,
                g_ogl.surface_tex_height,
                0,
                g_ogl.surface_format = GL_RGB,
                g_ogl.surface_type = GL_UNSIGNED_SHORT_5_6_5,
                0);


            if (glGetError() != GL_NO_ERROR)
            {
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_RGB5,
                    g_ogl.surface_tex_width,
                    g_ogl.surface_tex_height,
                    0,
                    g_ogl.surface_format = GL_RGB,
                    g_ogl.surface_type = GL_UNSIGNED_SHORT_5_6_5,
                    0);
            }
        }
        else if (g_ddraw.bpp == 8)
        {
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_LUMINANCE8,
                g_ogl.surface_tex_width,
                g_ogl.surface_tex_height,
                0,
                g_ogl.surface_format = GL_LUMINANCE,
                g_ogl.surface_type = GL_UNSIGNED_BYTE,
                0);


            if (glGetError() != GL_NO_ERROR)
            {
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_R8,
                    g_ogl.surface_tex_width,
                    g_ogl.surface_tex_height,
                    0,
                    g_ogl.surface_format = GL_RED,
                    g_ogl.surface_type = GL_UNSIGNED_BYTE,
                    0);
            }

            if (glGetError() != GL_NO_ERROR)
            {
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_RED,
                    g_ogl.surface_tex_width,
                    g_ogl.surface_tex_height,
                    0,
                    g_ogl.surface_format = GL_RED,
                    g_ogl.surface_type = GL_UNSIGNED_BYTE,
                    0);
            }
        }
    }

    if (g_ddraw.bpp == 8)
    {
        glGenTextures(TEXTURE_COUNT, g_ogl.palette_tex_ids);

        for (int i = 0; i < TEXTURE_COUNT; i++)
        {
            glBindTexture(GL_TEXTURE_2D, g_ogl.palette_tex_ids[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        }
    }
}

static void ogl_init_main_program()
{
    if (!g_ogl.main_program)
        return;

    glUseProgram(g_ogl.main_program);

    glUniform1i(glGetUniformLocation(g_ogl.main_program, "Texture"), 0);

    if (g_ddraw.bpp == 8)
        glUniform1i(glGetUniformLocation(g_ogl.main_program, "PaletteTexture"), 1);

    g_ogl.main_vertex_coord_attr_loc = glGetAttribLocation(g_ogl.main_program, "VertexCoord");
    g_ogl.main_tex_coord_attr_loc = glGetAttribLocation(g_ogl.main_program, "TexCoord");

    glGenBuffers(3, g_ogl.main_vbos);

    /* Into frame buffer 0, the right way up for the shaders */
    glBindBuffer(GL_ARRAY_BUFFER, g_ogl.main_vbos[0]);
    static const GLfloat vertex_coord[] = {
        -1.0f,-1.0f,
        -1.0f, 1.0f,
         1.0f, 1.0f,
         1.0f,-1.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_coord), vertex_coord, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ARRAY_BUFFER, g_ogl.main_vbos[1]);
    GLfloat tex_coord[] = {
        0.0f,          0.0f,
        0.0f,          g_ogl.scale_h,
        g_ogl.scale_w, g_ogl.scale_h,
        g_ogl.scale_w, 0.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coord), tex_coord, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenVertexArrays(1, &g_ogl.main_vao);
    glBindVertexArray(g_ogl.main_vao);

    glBindBuffer(GL_ARRAY_BUFFER, g_ogl.main_vbos[0]);
    glVertexAttribPointer(g_ogl.main_vertex_coord_attr_loc, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(g_ogl.main_vertex_coord_attr_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ARRAY_BUFFER, g_ogl.main_vbos[1]);
    glVertexAttribPointer(g_ogl.main_tex_coord_attr_loc, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(g_ogl.main_tex_coord_attr_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ogl.main_vbos[2]);
    static const GLushort indices[] =
    {
        0, 1, 2,
        0, 2, 3,
    };
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    const float mvp_matrix[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1,
    };
    glUniformMatrix4fv(glGetUniformLocation(g_ogl.main_program, "MVPMatrix"), 1, GL_FALSE, mvp_matrix);
}

/* A full-screen quad for DOSBox Staging's shaders: they take 'a_position'
 * and work out their texture coordinates from it */
static void ogl_create_quad(GLuint program, GLuint vbos[2], GLuint* vao)
{
    GLint vertex_coord_attr_loc = glGetAttribLocation(program, "a_position");

    glGenBuffers(2, vbos);

    glBindBuffer(GL_ARRAY_BUFFER, vbos[0]);
    static const GLfloat vertex_coord[] = {
        -1.0f, 1.0f,
         1.0f, 1.0f,
         1.0f,-1.0f,
        -1.0f,-1.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_coord), vertex_coord, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenVertexArrays(1, vao);
    glBindVertexArray(*vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbos[0]);
    glVertexAttribPointer(vertex_coord_attr_loc, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(vertex_coord_attr_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos[1]);
    static const GLushort indices[] =
    {
        0, 1, 2,
        0, 2, 3,
    };
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);
}

/* shader1: the image adjustments, with the two frame buffers. DOSBox
 * Staging's shaders read the whole texture, so these are exactly the image's
 * size. The game's picture goes into the first; the image-adjustments pass
 * reads it unfiltered and writes the doubled image the shader expects into
 * the second, which that shader reads filtered, unless it's nearest */
static void ogl_init_shader1_program()
{
    if (!g_ogl.shader1_program || !g_ogl.shader2_program)
        return;

    ogl_create_quad(g_ogl.shader1_program, g_ogl.shader1_vbos, &g_ogl.shader1_vao);

    glGenFramebuffers(FBO_COUNT, g_ogl.frame_buffer_id);
    glGenTextures(FBO_COUNT, g_ogl.frame_buffer_tex_id);

    for (int i = 0; i < FBO_COUNT; i++)
    {
        int fbo_width = i == 0 ? (int)g_ddraw.width : g_ddraw.render.crt.render_width;
        int fbo_height = i == 0 ? (int)g_ddraw.height : g_ddraw.render.crt.render_height;
        GLint fbo_filter =
            i == 0 || g_ddraw.render.crt.shader == CRT_SHADER_NEAREST ? GL_NEAREST : GL_LINEAR;

        glBindFramebuffer(GL_FRAMEBUFFER, g_ogl.frame_buffer_id[i]);

        glBindTexture(GL_TEXTURE_2D, g_ogl.frame_buffer_tex_id[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fbo_filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fbo_filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            fbo_width,
            fbo_height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            0);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_ogl.frame_buffer_tex_id[i], 0);

        GLenum draw_buffers[1] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, draw_buffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            TRACE("     crt-auto: frame buffer %d (%dx%d) is incomplete\n", i, fbo_width, fbo_height);

            /* ogl_init_crt_auto_uniforms sees the programs gone */
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glUseProgram(0);

            glDeleteTextures(FBO_COUNT, g_ogl.frame_buffer_tex_id);

            if (glDeleteFramebuffers)
                glDeleteFramebuffers(FBO_COUNT, g_ogl.frame_buffer_id);

            if (glDeleteBuffers)
                glDeleteBuffers(2, g_ogl.shader1_vbos);

            if (glDeleteVertexArrays)
                glDeleteVertexArrays(1, &g_ogl.shader1_vao);

            if (glDeleteProgram)
            {
                glDeleteProgram(g_ogl.shader1_program);
                glDeleteProgram(g_ogl.shader2_program);
            }

            g_ogl.shader1_program = 0;
            g_ogl.shader2_program = 0;
            return;
        }

        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void ogl_init_shader2_program()
{
    if (!g_ogl.shader1_program || !g_ogl.shader2_program)
        return;

    ogl_create_quad(g_ogl.shader2_program, g_ogl.shader2_vbos, &g_ogl.shader2_vao);
}

/* A shader or preset built into the DLL (res.rc), as a NUL-terminated copy */
static char* ogl_load_text_resource(const char* name)
{
    HRSRC res = FindResourceA(g_ddraw_module, name, MAKEINTRESOURCEA(10) /* RT_RCDATA */);
    if (!res)
        return NULL;

    HGLOBAL handle = LoadResource(g_ddraw_module, res);
    DWORD size = SizeofResource(g_ddraw_module, res);
    const void* data = handle ? LockResource(handle) : NULL;

    if (!data || !size)
        return NULL;

    char* text = malloc(size + 1);
    if (text)
    {
        memcpy(text, data, size);
        text[size] = '\0';
    }

    return text;
}

/* shader1: DOSBox Staging's image-adjustments pass. shader2: the shader
 * crt-auto chose for this mode and window size, with its preset */
static void ogl_build_crt_auto_programs(BOOL core_profile)
{
    const char* shader_name = NULL;
    const char* preset_name = NULL;

    switch (g_ddraw.render.crt.shader)
    {
    case CRT_SHADER_VGA_1080P:
        shader_name = "CRT_VGA_1080P";
        break;
    case CRT_SHADER_VGA_1080P_FAKE_DOUBLE_SCAN:
        shader_name = "CRT_VGA_1080P_FAKE_DOUBLE_SCAN";
        break;
    case CRT_SHADER_HYLLIAN_VGA_1440P:
        shader_name = "CRT_HYLLIAN";
        preset_name = "CRT_PRESET_VGA_1440P";
        break;
    case CRT_SHADER_HYLLIAN_VGA_4K:
        shader_name = "CRT_HYLLIAN";
        preset_name = "CRT_PRESET_VGA_4K";
        break;
    case CRT_SHADER_NEAREST:
        shader_name = "CRT_NEAREST";
        break;
    case CRT_SHADER_BILINEAR:
        shader_name = "CRT_BILINEAR";
        break;
    case CRT_SHADER_SHARP:
    default:
        shader_name = "CRT_SHARP";
        break;
    }

    char* adjustments = ogl_load_text_resource("CRT_IMAGE_ADJUSTMENTS");
    char* shader = ogl_load_text_resource(shader_name);
    char* preset = preset_name ? ogl_load_text_resource(preset_name) : NULL;

    memset(&g_ogl.crt_params, 0, sizeof(g_ogl.crt_params));

    if (adjustments && shader && (preset || !preset_name))
    {
        g_ogl.shader1_program = oglu_build_program_from_source(adjustments, core_profile);
        g_ogl.shader2_program = oglu_build_program_from_source(shader, core_profile);

        /* The shader's '#pragma parameter' defaults, then the preset */
        crt_auto_params_from_shader(&g_ogl.crt_params, shader);

        if (preset)
            crt_auto_params_apply_preset(&g_ogl.crt_params, preset);
    }

    if (g_ogl.shader1_program && g_ogl.shader2_program)
    {
        g_ogl.crt_auto = TRUE;
    }
    else
    {
        TRACE(
            "     crt-auto: building %s failed (image adjustments %s, %s %s)\n",
            crt_auto_shader_name(g_ddraw.render.crt.shader),
            g_ogl.shader1_program ? "ok" : "failed",
            shader_name,
            g_ogl.shader2_program ? "ok" : "failed");

        if (glDeleteProgram)
        {
            if (g_ogl.shader1_program)
                glDeleteProgram(g_ogl.shader1_program);

            if (g_ogl.shader2_program)
                glDeleteProgram(g_ogl.shader2_program);
        }

        g_ogl.shader1_program = g_ogl.shader2_program = 0;
    }

    free(adjustments);
    free(shader);
    free(preset);
}

static void ogl_uniform1i(GLuint program, const char* name, int value)
{
    GLint loc = glGetUniformLocation(program, name);
    if (loc != -1)
        glUniform1i(loc, value);
}

static void ogl_uniform1f(GLuint program, const char* name, float value)
{
    GLint loc = glGetUniformLocation(program, name);
    if (loc != -1)
        glUniform1f(loc, value);
}

static void ogl_uniform2f(GLuint program, const char* name, float x, float y)
{
    GLint loc = glGetUniformLocation(program, name);
    if (loc != -1)
    {
        float v[2] = { x, y };
        glUniform2fv(loc, 1, v);
    }
}

static void ogl_uniform3f(GLuint program, const char* name, const float v[3])
{
    GLint loc = glGetUniformLocation(program, name);
    if (loc != -1)
        glUniform3fv(loc, 1, v);
}

/* The uniforms DOSBox Staging's shader pipeline sets (shader_pipeline.cpp) */
static void ogl_init_crt_auto_uniforms()
{
    if (!g_ogl.crt_auto)
        return;

    if (!g_ogl.shader1_program || !g_ogl.shader2_program)
    {
        /* The framebuffers failed */
        g_ogl.crt_auto = FALSE;
        return;
    }

    const CRT_AUTO_LAYOUT* crt = &g_ddraw.render.crt;
    CRT_AUTO_COLOR color = crt_auto_vga_color(&g_config.crt);

    GLuint p = g_ogl.shader1_program;
    glUseProgram(p);

    ogl_uniform1i(p, "INPUT_TEXTURE_0", 0);
    ogl_uniform2f(p, "INPUT_SIZE_0", (float)g_ddraw.width, (float)g_ddraw.height);
    ogl_uniform2f(p, "OUTPUT_START_OFFSET", 0.0f, 0.0f);
    ogl_uniform2f(p, "OUTPUT_SIZE", (float)crt->render_width, (float)crt->render_height);

    ogl_uniform1i(p, "COLOR_SPACE", color.color_space);
    ogl_uniform1i(p, "ENABLE_ADJUSTMENTS", color.enable_adjustments);
    ogl_uniform1i(p, "COLOR_PROFILE", color.color_profile);
    ogl_uniform1f(p, "BRIGHTNESS", color.brightness);
    ogl_uniform1f(p, "CONTRAST", color.contrast);
    ogl_uniform1f(p, "GAMMA", color.gamma);
    ogl_uniform1f(p, "DIGITAL_CONTRAST", color.digital_contrast);
    ogl_uniform3f(p, "BLACK_LEVEL_COLOR", color.black_level_color);
    ogl_uniform1f(p, "BLACK_LEVEL", color.black_level);
    ogl_uniform1f(p, "SATURATION", color.saturation);
    ogl_uniform1f(p, "COLOR_TEMPERATURE_KELVIN", color.color_temperature_kelvin);
    ogl_uniform1f(p, "COLOR_TEMPERATURE_LUMA_PRESERVE", color.color_temperature_luma_preserve);
    ogl_uniform1f(p, "RED_GAIN", color.red_gain);
    ogl_uniform1f(p, "GREEN_GAIN", color.green_gain);
    ogl_uniform1f(p, "BLUE_GAIN", color.blue_gain);

    p = g_ogl.shader2_program;
    glUseProgram(p);

    ogl_uniform1i(p, "INPUT_TEXTURE_0", 0);
    ogl_uniform2f(p, "INPUT_SIZE_0", (float)crt->render_width, (float)crt->render_height);

    ogl_uniform2f(
        p,
        "OUTPUT_START_OFFSET",
        (float)g_ddraw.render.viewport.x,
        (float)(g_ddraw.render.viewport.y + g_ddraw.render.opengl_y_align));

    ogl_uniform2f(
        p,
        "OUTPUT_SIZE",
        (float)g_ddraw.render.viewport.width,
        (float)g_ddraw.render.viewport.height);

    for (int i = 0; i < g_ogl.crt_params.count; i++)
    {
        ogl_uniform1f(p, g_ogl.crt_params.param[i].name, g_ogl.crt_params.param[i].value);
    }

    glUseProgram(0);

    TRACE("     crt-auto: using %s\n", crt_auto_shader_name(crt->shader));
}

/* GPU time of the crt-auto passes: for the status OSD, and in debug builds
 * logged as an average every 300 frames */
static BOOL ogl_gpu_time_begin()
{
    if (!glGenQueries || !glBeginQuery || !glEndQuery || !glGetQueryObjectiv || !glGetQueryObjectui64v)
        return FALSE;

    if (!g_ogl.gpu_time_query)
        glGenQueries(1, &g_ogl.gpu_time_query);

    if (g_ogl.gpu_time_pending)
    {
        GLint available = 0;
        glGetQueryObjectiv(g_ogl.gpu_time_query, GL_QUERY_RESULT_AVAILABLE, &available);

        if (!available)
            return FALSE;

        GLuint64 ns = 0;
        glGetQueryObjectui64v(g_ogl.gpu_time_query, GL_QUERY_RESULT, &ns);

        g_ogl.gpu_time_pending = FALSE;

        overlay_gpu_time(ns / 1000000.0);

#ifdef _DEBUG
        g_ogl.gpu_time_ms += ns / 1000000.0;

        if (++g_ogl.gpu_time_frames == 300)
        {
            TRACE(
                "crt-auto: GPU time %.3f ms per frame (%s, %ux%u to %dx%d, average of 300 frames)\n",
                g_ogl.gpu_time_ms / g_ogl.gpu_time_frames,
                crt_auto_shader_name(g_ddraw.render.crt.shader),
                g_ddraw.width,
                g_ddraw.height,
                g_ddraw.render.viewport.width,
                g_ddraw.render.viewport.height);

            g_ogl.gpu_time_ms = 0;
            g_ogl.gpu_time_frames = 0;
        }
#endif
    }

    glBeginQuery(GL_TIME_ELAPSED, g_ogl.gpu_time_query);
    return TRUE;
}

static void ogl_gpu_time_end()
{
    glEndQuery(GL_TIME_ELAPSED);
    g_ogl.gpu_time_pending = TRUE;
}

/* The finished frame, for the screenshot key: read back before SwapBuffers */
static void ogl_screenshot()
{
    RECT rc;

    /* The window's extra row (opengl_y_align) is at the bottom, off the screen */
    int y = g_ddraw.render.opengl_y_align;

    if (!glReadPixels || !real_GetClientRect(g_ddraw.hwnd, &rc) || rc.right <= 0 || rc.bottom - y <= 0)
        return;

    int w = rc.right, h = rc.bottom - y;
    unsigned char* buf = malloc((size_t)w * h * 4);

    if (buf)
    {
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);

        if (glGetError() == GL_NO_ERROR)
            ss_save_shaded(buf, w, h, w * 4, FALSE, TRUE);

        free(buf);
    }
}

static void ogl_render()
{
    if (!g_ogl.crt_auto)
    {
        g_ogl.use_opengl = FALSE;
        return;
    }

    DWORD timeout = g_config.minfps > 0 ? g_ddraw.minfps_tick_len : INFINITE;

    /* As oglu_build_program */
    const char* glsl_version = wglCreateContextAttribsARB ? "#version 150" : "#version 130";

    while (g_ogl.use_opengl && g_ddraw.render.run &&
        (g_config.minfps < 0 ||
            WaitForSingleObject(g_ddraw.render.sem, overlay_wait_timeout(timeout)) != WAIT_FAILED) &&
        g_ddraw.render.run)
    {
#if _DEBUG
        dbg_draw_frame_info_start();
#endif

        g_ogl.scale_w = (float)g_ddraw.width / g_ogl.surface_tex_width;
        g_ogl.scale_h = (float)g_ddraw.height / g_ogl.surface_tex_height;

        static int tex_index = 0, pal_index = 0;

        fpsl_frame_start();

        BOOL screenshot = ss_begin();
        BOOL new_frame = FALSE;

        EnterCriticalSection(&g_ddraw.cs);

        if (g_ddraw.primary &&
            g_ddraw.primary->bpp == g_ddraw.bpp &&
            g_ddraw.primary->width == g_ddraw.width &&
            g_ddraw.primary->height == g_ddraw.height &&
            (g_ddraw.bpp != 8 || g_ddraw.primary->palette))
        {
            if (g_config.lock_surfaces)
                EnterCriticalSection(&g_ddraw.primary->cs);

            if (g_ddraw.bpp == 8 &&
                (InterlockedExchange(&g_ddraw.render.palette_updated, FALSE) || g_config.minfps == -2))
            {
                if (++pal_index >= TEXTURE_COUNT)
                    pal_index = 0;

                glBindTexture(GL_TEXTURE_2D, g_ogl.palette_tex_ids[pal_index]);
                new_frame = TRUE;

                glTexSubImage2D(
                    GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    256,
                    1,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    g_ddraw.primary->palette->data_bgr);
            }

            BOOL surface_updated = InterlockedExchange(&g_ddraw.render.surface_updated, FALSE);
            new_frame = new_frame || surface_updated;

            if (surface_updated || g_config.minfps == -2)
            {
                if (++tex_index >= TEXTURE_COUNT)
                    tex_index = 0;

                glBindTexture(GL_TEXTURE_2D, g_ogl.surface_tex_ids[tex_index]);

                DWORD row_len = g_ddraw.primary->pitch ? g_ddraw.primary->pitch / g_ddraw.primary->bytes_pp : 0;

                if (row_len != g_ddraw.primary->width)
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_len);

                glTexSubImage2D(
                    GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    g_ddraw.width,
                    g_ddraw.height,
                    g_ogl.surface_format,
                    g_ogl.surface_type,
                    g_ddraw.primary->surface);

                if (row_len != g_ddraw.primary->width)
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }

            if (screenshot)
                ss_save_raw(g_ddraw.primary);

            static int error_check_count = 0;

            if (error_check_count < 10)
            {
                error_check_count++;

                GLenum err = glGetError();

                if (err != GL_NO_ERROR && err != GL_INVALID_FRAMEBUFFER_OPERATION)
                {
                    g_ogl.use_opengl = FALSE;

                    TRACE("OpenGL error %08x (ogl_render())\n", err);
                    ogl_check_error("ogl_render()");
                }
            }

            if (g_config.fixchilds)
            {
                g_ddraw.child_window_exists = FALSE;
                EnumChildWindows(g_ddraw.hwnd, util_enum_child_proc, (LPARAM)g_ddraw.primary);

                if (g_ddraw.child_window_exists &&
                    (g_ddraw.render.width != g_ddraw.width || g_ddraw.render.height != g_ddraw.height))
                {
                    glClear(GL_COLOR_BUFFER_BIT);
                }
            }

            if (g_config.lock_surfaces)
                LeaveCriticalSection(&g_ddraw.primary->cs);
        }

        LeaveCriticalSection(&g_ddraw.cs);

        if (g_ddraw.render.viewport.x != 0 || g_ddraw.render.viewport.y != 0)
        {
            glClear(GL_COLOR_BUFFER_BIT);
        }

        if (glActiveTexture)
            glActiveTexture(GL_TEXTURE0);

        glBindTexture(GL_TEXTURE_2D, g_ogl.surface_tex_ids[tex_index]);

        if (g_ddraw.bpp == 8)
        {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, g_ogl.palette_tex_ids[pal_index]);

            glActiveTexture(GL_TEXTURE0);
        }

        {
#ifdef _DEBUG
            BOOL gpu_timing = ogl_gpu_time_begin();
#else
            BOOL gpu_timing = overlay_status_visible() && ogl_gpu_time_begin();
#endif

            /* draw surface into framebuffer */
            glUseProgram(g_ogl.main_program);

            glViewport(0, 0, g_ddraw.width, g_ddraw.height);

            glBindFramebuffer(GL_FRAMEBUFFER, g_ogl.frame_buffer_id[0]);

            glBindVertexArray(g_ogl.main_vao);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glBindVertexArray(0);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);

            /* image adjustments */
            glViewport(0, 0, g_ddraw.render.crt.render_width, g_ddraw.render.crt.render_height);

            glUseProgram(g_ogl.shader1_program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_ogl.frame_buffer_tex_id[0]);

            glBindFramebuffer(GL_FRAMEBUFFER, g_ogl.frame_buffer_id[1]);

            glBindVertexArray(g_ogl.shader1_vao);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glBindVertexArray(0);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            /* the shader */

            if (g_ddraw.child_window_exists)
            {
                glViewport(0, g_ddraw.render.height - g_ddraw.height, g_ddraw.width, g_ddraw.height);
            }
            else
            {
                glViewport(
                    g_ddraw.render.viewport.x,
                    g_ddraw.render.viewport.y + g_ddraw.render.opengl_y_align,
                    g_ddraw.render.viewport.width,
                    g_ddraw.render.viewport.height);
            }

            glUseProgram(g_ogl.shader2_program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_ogl.frame_buffer_tex_id[1]);

            glBindVertexArray(g_ogl.shader2_vao);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glBindVertexArray(0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);

            if (gpu_timing)
                ogl_gpu_time_end();
        }

        if (g_ddraw.bnet_active)
            glClear(GL_COLOR_BUFFER_BIT);

        if (screenshot)
            ogl_screenshot();

        /* After the screenshot: the shaded one is without the OSD */
        {
            RECT rc;

            /* overlay_ogl_draw knows whether there's an OSD to draw */
            if (real_GetClientRect(g_ddraw.hwnd, &rc) &&
                rc.right > 0 && rc.bottom - g_ddraw.render.opengl_y_align > 0)
            {
                overlay_ogl_draw(rc.right, rc.bottom, rc.bottom - g_ddraw.render.opengl_y_align, glsl_version);
            }
        }

        SwapBuffers(g_ogl.hdc);

        overlay_frame_presented(new_frame);

        /* Force redraw for GDI games (ClueFinders) */
        if (!g_ddraw.primary)
        {
            RedrawWindow(g_ddraw.hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
        }

        if (!g_ddraw.render.run)
            break;

#if _DEBUG
        dbg_draw_frame_info_end();
#endif

        fpsl_frame_end();
    }
}

static BOOL ogl_release_resources()
{
    overlay_ogl_release();

    glDeleteTextures(TEXTURE_COUNT, g_ogl.surface_tex_ids);

    if (g_ogl.gpu_time_query && glDeleteQueries)
        glDeleteQueries(1, &g_ogl.gpu_time_query);

    g_ogl.gpu_time_query = 0;
    g_ogl.gpu_time_pending = FALSE;
    g_ogl.gpu_time_ms = 0;
    g_ogl.gpu_time_frames = 0;

    if (g_ddraw.bpp == 8)
        glDeleteTextures(TEXTURE_COUNT, g_ogl.palette_tex_ids);

    if (glUseProgram)
        glUseProgram(0);

    if (g_ogl.shader1_program)
    {
        glDeleteTextures(FBO_COUNT, g_ogl.frame_buffer_tex_id);

        if (glDeleteBuffers)
            glDeleteBuffers(2, g_ogl.shader1_vbos);

        if (glDeleteFramebuffers)
            glDeleteFramebuffers(FBO_COUNT, g_ogl.frame_buffer_id);

        if (glDeleteVertexArrays)
            glDeleteVertexArrays(1, &g_ogl.shader1_vao);
    }

    if (g_ogl.shader2_program)
    {
        if (glDeleteBuffers)
            glDeleteBuffers(2, g_ogl.shader2_vbos);

        if (glDeleteVertexArrays)
            glDeleteVertexArrays(1, &g_ogl.shader2_vao);
    }

    if (glDeleteProgram)
    {
        if (g_ogl.main_program)
            glDeleteProgram(g_ogl.main_program);

        if (g_ogl.shader1_program)
            glDeleteProgram(g_ogl.shader1_program);

        if (g_ogl.shader2_program)
            glDeleteProgram(g_ogl.shader2_program);
    }

    if (g_ogl.main_program)
    {
        if (glDeleteBuffers)
            glDeleteBuffers(3, g_ogl.main_vbos);

        if (glDeleteVertexArrays)
            glDeleteVertexArrays(1, &g_ogl.main_vao);
    }

    return TRUE;
}

BOOL ogl_release()
{
    if (g_ddraw.render.thread)
        return FALSE;

    ogl_free();

    return TRUE;
}

/* Also from the render thread, when it can't start */
static void ogl_free()
{
    if (g_ogl.context)
    {
        xwglMakeCurrent(NULL, NULL);
        xwglDeleteContext(g_ogl.context);
        g_ogl.context = NULL;
    }
}

static BOOL ogl_texture_upload_test()
{
    if (g_ogl.surface_tex_width > 4096 || g_ogl.surface_tex_height > 4096)
        return TRUE;

    int* surface_tex =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_ogl.surface_tex_width * g_ogl.surface_tex_height * sizeof(int));

    if (!surface_tex)
        return TRUE;

    static char test_data[] = { 0,1,2,0,0,2,3,0,0,4,5,0,0,6,7,0,0,8,9,0 };

    int i;
    for (i = 0; i < TEXTURE_COUNT; i++)
    {
        memcpy(surface_tex, test_data, sizeof(test_data));

        glBindTexture(GL_TEXTURE_2D, g_ogl.surface_tex_ids[i]);

        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            g_ddraw.width,
            g_ddraw.height,
            g_ogl.surface_format,
            g_ogl.surface_type,
            surface_tex);

        memset(surface_tex, 0, sizeof(test_data));

        glGetTexImage(GL_TEXTURE_2D, 0, g_ogl.surface_format, g_ogl.surface_type, surface_tex);

        if (memcmp(surface_tex, test_data, sizeof(test_data)) != 0)
        {
            HeapFree(GetProcessHeap(), 0, surface_tex);
            return FALSE;
        }
    }

    if (g_ddraw.bpp == 8)
    {
        for (i = 0; i < TEXTURE_COUNT; i++)
        {
            glBindTexture(GL_TEXTURE_2D, g_ogl.palette_tex_ids[i]);

            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                256,
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                surface_tex);

            memset(surface_tex, 0, sizeof(test_data));

            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface_tex);

            if (memcmp(surface_tex, test_data, sizeof(test_data)) != 0)
            {
                HeapFree(GetProcessHeap(), 0, surface_tex);
                return FALSE;
            }

        }
    }

    HeapFree(GetProcessHeap(), 0, surface_tex);
    return TRUE;
}

static BOOL ogl_shader_test()
{
    BOOL result = TRUE;

    if (g_ddraw.bpp != 8 || g_ogl.surface_tex_width > 4096 || g_ogl.surface_tex_height > 4096)
        return result;

    int* surface_tex =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_ogl.surface_tex_width * g_ogl.surface_tex_height * sizeof(int));

    if (!surface_tex)
        return TRUE;

    if (g_ogl.main_program)
    {
        memset(surface_tex, 0, g_ogl.surface_tex_height * g_ogl.surface_tex_width * sizeof(int));

        GLuint fbo_id = 0;
        glGenFramebuffers(1, &fbo_id);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);

        GLuint fbo_tex_id = 0;
        glGenTextures(1, &fbo_tex_id);
        glBindTexture(GL_TEXTURE_2D, fbo_tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            g_ogl.surface_tex_width,
            g_ogl.surface_tex_height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            surface_tex);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex_id, 0);

        GLenum draw_buffers[1] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, draw_buffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
        {
            static char gray0pal[] = { 128,128,128,128 };

            glBindTexture(GL_TEXTURE_2D, g_ogl.palette_tex_ids[0]);

            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                1,
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                gray0pal);

            glBindTexture(GL_TEXTURE_2D, g_ogl.surface_tex_ids[0]);

            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                g_ogl.surface_tex_width,
                g_ogl.surface_tex_height,
                g_ogl.surface_format,
                GL_UNSIGNED_BYTE,
                surface_tex);

            glViewport(0, 0, g_ogl.surface_tex_width, g_ogl.surface_tex_height);

            glUseProgram(g_ogl.main_program);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, g_ogl.palette_tex_ids[0]);
            glActiveTexture(GL_TEXTURE0);

            glBindVertexArray(g_ogl.main_vao);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glBindVertexArray(0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);

            glBindTexture(GL_TEXTURE_2D, fbo_tex_id);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface_tex);

            if (glGetError() == GL_NO_ERROR)
            {
                int i;
                for (i = 0; i < g_ogl.surface_tex_height * g_ogl.surface_tex_width; i++)
                {
                    if (surface_tex[i] != 0x80808080)
                    {
                        result = FALSE;
                        break;
                    }
                }
            }
        }

        glBindTexture(GL_TEXTURE_2D, 0);

        if (glDeleteFramebuffers)
            glDeleteFramebuffers(1, &fbo_id);

        glDeleteTextures(1, &fbo_tex_id);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    while (glGetError() != GL_NO_ERROR);

    HeapFree(GetProcessHeap(), 0, surface_tex);
    return result;
}
