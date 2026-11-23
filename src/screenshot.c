#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include "dd.h"
#include "ddpalette.h"
#include "ddsurface.h"
#include "lodepng.h"
#include "blt.h"
#include "config.h"
#include "debug.h"
#include "screenshot.h"

/* The screenshot key asks for both pictures of the next frame: the game's own
   surface ("raw") and what's on screen with the CRT shader ("crt"). The render
   thread takes them and a worker thread encodes the PNGs, which takes a while
   at 4K. */

static volatile LONG g_ss_pending;
static char g_ss_base[MAX_PATH]; /* render thread only */

typedef struct SS_JOB
{
    char filename[MAX_PATH];
    unsigned char* pixels; /* RGBA, or palette indices */
    unsigned int width;
    unsigned int height;
    BOOL indexed;
    RGBQUAD palette[256];
} SS_JOB;

static DWORD WINAPI ss_encode_main(LPVOID param)
{
    SS_JOB* job = (SS_JOB*)param;
    unsigned int error;

    if (job->indexed)
    {
        LodePNGState state;
        lodepng_state_init(&state);

        for (int i = 0; i < 256; i++)
        {
            RGBQUAD* c = &job->palette[i];
            lodepng_palette_add(&state.info_png.color, c->rgbRed, c->rgbGreen, c->rgbBlue, 255);
            lodepng_palette_add(&state.info_raw, c->rgbRed, c->rgbGreen, c->rgbBlue, 255);
        }

        state.info_png.color.colortype = LCT_PALETTE;
        state.info_png.color.bitdepth = 8;
        state.info_raw.colortype = LCT_PALETTE;
        state.info_raw.bitdepth = 8;
        state.encoder.auto_convert = 0;

        unsigned char* png = NULL;
        size_t png_size = 0;

        error = lodepng_encode(&png, &png_size, job->pixels, job->width, job->height, &state);

        if (!error && png)
            error = lodepng_save_file(png, png_size, job->filename);

        lodepng_state_cleanup(&state);
        free(png);
    }
    else
    {
        error = lodepng_encode32_file(job->filename, job->pixels, job->width, job->height);
    }

    if (error)
        TRACE("Screenshot %s failed (lodepng error %u)\n", job->filename, error);

    free(job->pixels);
    free(job);

    return 0;
}

static void ss_encode(SS_JOB* job)
{
    HANDLE thread = CreateThread(NULL, 0, ss_encode_main, job, CREATE_SUSPENDED, NULL);

    if (thread)
    {
        SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        ResumeThread(thread);
        CloseHandle(thread);
    }
    else
    {
        ss_encode_main(job);
    }
}

static SS_JOB* ss_new_job(const char* suffix, unsigned int width, unsigned int height, unsigned int bytes_pp)
{
    SS_JOB* job = calloc(1, sizeof(SS_JOB));

    if (!job)
        return NULL;

    job->pixels = malloc((size_t)width * height * bytes_pp);

    if (!job->pixels)
    {
        free(job);
        return NULL;
    }

    _snprintf(job->filename, sizeof(job->filename) - 1, "%s_%s.png", g_ss_base, suffix);
    job->width = width;
    job->height = height;

    return job;
}

void ss_request()
{
    InterlockedExchange(&g_ss_pending, TRUE);

    /* A game that isn't drawing leaves the render thread waiting */
    if (g_ddraw.render.sem)
        ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
}

BOOL ss_begin()
{
    if (!InterlockedExchange(&g_ss_pending, FALSE))
        return FALSE;

    char title[sizeof(g_ddraw.title)];
    strncpy(title, g_ddraw.title, sizeof(title) - 1);
    title[sizeof(title) - 1] = 0;

    for (char* c = title; *c; c++)
    {
        if (*c == ' ' || strchr("\\/:*?\"<>|", *c))
        {
            *c = '_';
        }
        else
        {
            *c = (char)tolower((unsigned char)*c);
        }
    }

    char str_time[64];
    time_t t = time(NULL);
    strftime(str_time, sizeof(str_time), "%Y-%m-%d_%H-%M-%S", localtime(&t));

    CreateDirectoryA(g_config.screenshot_dir, NULL);

    /* Two in the same second get a number */
    for (int i = 1; i < 100; i++)
    {
        char raw[MAX_PATH], crt[MAX_PATH];

        if (i == 1)
            _snprintf(g_ss_base, sizeof(g_ss_base) - 1, "%s%s_%s", g_config.screenshot_dir, title, str_time);
        else
            _snprintf(g_ss_base, sizeof(g_ss_base) - 1, "%s%s_%s_%d", g_config.screenshot_dir, title, str_time, i);

        g_ss_base[sizeof(g_ss_base) - 1] = 0;

        _snprintf(raw, sizeof(raw) - 1, "%s_raw.png", g_ss_base);
        _snprintf(crt, sizeof(crt) - 1, "%s_crt.png", g_ss_base);
        raw[sizeof(raw) - 1] = crt[sizeof(crt) - 1] = 0;

        if (!FILE_EXISTS(raw) && !FILE_EXISTS(crt))
            return TRUE;
    }

    return FALSE;
}

void ss_save_raw(IDirectDrawSurfaceImpl* src)
{
    if (!src || !dds_GetBuffer(src) || !src->width || !src->height)
        return;

    unsigned char* surface = dds_GetBuffer(src);
    SS_JOB* job;

    if (src->bpp == 8)
    {
        if (!src->palette || !(job = ss_new_job("raw", src->width, src->height, 1)))
            return;

        job->indexed = TRUE;
        memcpy(job->palette, src->palette->data_rgb, sizeof(job->palette));

        for (DWORD y = 0; y < src->height; y++)
            memcpy(job->pixels + y * src->width, surface + y * src->pitch, src->width);
    }
    else
    {
        if (!(job = ss_new_job("raw", src->width, src->height, 4)))
            return;

        unsigned int* dst = (unsigned int*)job->pixels;
        int dst_p = src->width * 4;

        if (src->bpp == 16 && g_config.rgb555)
            blt_rgb555_to_rgba8888(dst, 0, 0, src->width, src->height, dst_p, (void*)surface, 0, 0, src->pitch);
        else if (src->bpp == 16)
            blt_rgb565_to_rgba8888(dst, 0, 0, src->width, src->height, dst_p, (void*)surface, 0, 0, src->pitch);
        else if (src->bpp == 24)
            blt_bgr888_to_rgba8888(dst, 0, 0, src->width, src->height, dst_p, (void*)surface, 0, 0, src->pitch);
        else if (src->bpp == 32)
            blt_bgra8888_to_rgba8888(dst, 0, 0, src->width, src->height, dst_p, (void*)surface, 0, 0, src->pitch);
        else
        {
            free(job->pixels);
            free(job);
            return;
        }
    }

    ss_encode(job);
}

void ss_save_shaded(const void* pixels, int width, int height, int pitch, BOOL bgra, BOOL bottom_up)
{
    if (!pixels || width <= 0 || height <= 0)
        return;

    SS_JOB* job = ss_new_job("crt", width, height, 4);

    if (!job)
        return;

    for (int y = 0; y < height; y++)
    {
        const unsigned char* s = (const unsigned char*)pixels + (size_t)pitch * (bottom_up ? height - 1 - y : y);
        unsigned char* d = job->pixels + (size_t)width * 4 * y;

        for (int x = 0; x < width; x++, s += 4, d += 4)
        {
            d[0] = bgra ? s[2] : s[0];
            d[1] = s[1];
            d[2] = bgra ? s[0] : s[2];
            d[3] = 255;
        }
    }

    ss_encode(job);
}
