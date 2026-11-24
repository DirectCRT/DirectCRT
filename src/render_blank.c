#include <windows.h>
#include "dd.h"
#include "hook.h"
#include "debug.h"
#include "render_blank.h"

#define BLANK_REDRAW_MS 500

static const char g_blank_text[] =
    "DirectCRT could not start Direct3D 11 or OpenGL, so it can't show the game.\n"
    "\n"
    "Updating the graphics driver may help. To play without DirectCRT,\n"
    "remove its ddraw.dll from the game's folder.";

static void blank_draw(void)
{
    RECT rc;

    if (!g_ddraw.render.hdc || !real_GetClientRect(g_ddraw.hwnd, &rc))
        return;

    FillRect(g_ddraw.render.hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    /* Sized to the window, to read the same at any resolution */
    int size = max(16, (rc.bottom - rc.top) / 32);
    HFONT font = CreateFontA(
        -size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    HGDIOBJ old_font = SelectObject(g_ddraw.render.hdc, font ? (HGDIOBJ)font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(g_ddraw.render.hdc, TRANSPARENT);
    SetTextColor(g_ddraw.render.hdc, RGB(255, 255, 255));

    /* Centred: measured first, as DT_VCENTER is for one line only */
    RECT text = rc;
    DrawTextA(g_ddraw.render.hdc, g_blank_text, -1, &text, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);

    int height = text.bottom - text.top;
    text.left = rc.left;
    text.right = rc.right;
    text.top = rc.top + (rc.bottom - rc.top - height) / 2;
    text.bottom = text.top + height;

    DrawTextA(g_ddraw.render.hdc, g_blank_text, -1, &text, DT_CENTER | DT_WORDBREAK);

    SelectObject(g_ddraw.render.hdc, old_font);

    if (font)
        DeleteObject(font);
}

DWORD WINAPI blank_render_main(void)
{
    TRACE("     Neither Direct3D 11 nor OpenGL started: a black window\n");

    DWORD last = 0;

    /* Redrawn now and then, not with every picture the game sends */
    while (g_ddraw.render.run &&
        WaitForSingleObject(g_ddraw.render.sem, BLANK_REDRAW_MS) != WAIT_FAILED &&
        g_ddraw.render.run)
    {
        DWORD now = timeGetTime();

        if (!last || now - last >= BLANK_REDRAW_MS)
        {
            blank_draw();
            last = now;
        }
    }

    return 0;
}
