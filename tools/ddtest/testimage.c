/*
 * The built-in test image: a 4500 x 3000 landscape photo (testimage.png),
 * embedded in the program as a resource, so there's no file to look for.
 *
 * The Test image pattern shows it 1:1, centred and cropped, and Ctrl+arrows
 * move it around. A photo has what drawn patterns lack: fine detail, natural
 * colour, and a large, smooth sky, which 24 and 32-bit modes show smooth and
 * 16-bit modes show in bands.
 */

#include <stdlib.h>

#include "ddtest.h"
#include "resource.h"

static uint32_t* g_img;
static int g_w, g_h;
static BOOL g_tried;

const uint32_t* testimage_get(int* w, int* h)
{
    if (!g_img && !g_tried)
    {
        g_tried = TRUE; /* decoding takes a moment; don't retry every frame if it fails */

        HMODULE mod = GetModuleHandleA(NULL);
        HRSRC res = FindResourceA(mod, MAKEINTRESOURCEA(IDR_TESTIMAGE), MAKEINTRESOURCEA(10) /* RT_RCDATA */);
        HGLOBAL mem = res ? LoadResource(mod, res) : NULL;
        const void* data = mem ? LockResource(mem) : NULL;
        DWORD size = res ? SizeofResource(mod, res) : 0;

        if (data && size)
            image_load_memory(data, size, &g_img, &g_w, &g_h, NULL, 0);
    }

    *w = g_img ? g_w : 0;
    *h = g_img ? g_h : 0;
    return g_img;
}

void testimage_free(void)
{
    free(g_img);
    g_img = NULL;
    g_tried = FALSE;
}
