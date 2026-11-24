#include <windows.h>
#include <windowsx.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "dd.h"
#include "config.h"
#include "hook.h"
#include "ini.h"
#include "mouse.h"
#include "settings.h"
#include "wndproc.h"
#include "overlay.h"

/* The settings OSD's state, its input and directcrt.ini's values.
 *
 * Opening and closing, and the input, are on the game's thread (the one with
 * the window): the window procedure puts the keyboard and mouse messages in a
 * queue, and the render thread, which draws the OSD, takes them. The values
 * are only used on the render thread: read when the OSD opens, and written
 * with WritePrivateProfileString, after which the game's thread is asked to
 * apply them (WM_OVERLAY_MENU) */

#define MENU_QUEUE 512
#define MENU_VALUE_SIZE 264

static volatile LONG g_menu_open;
static volatile LONG g_capturing;
static BOOL g_relock;          /* the cursor was locked when the OSD opened */
static BOOL g_keys_down[256];  /* when it opened: the game saw these go down */
static int g_buttons;          /* held mouse buttons, a bit each */

static SRWLOCK g_queue_lock = SRWLOCK_INIT;
static OVERLAY_EVENT g_queue[MENU_QUEUE];
static int g_queue_head;
static int g_queue_count;

typedef struct MENU_VALUE
{
    char value[MENU_VALUE_SIZE];
    BOOL set;
} MENU_VALUE;

static MENU_VALUE* g_values;
static volatile LONG g_values_stale = 1;

/* A change is applied when the OSD stands still: applying stops the render
 * thread a moment, a stutter that mustn't fall in the middle of a slide */
static volatile LONG g_apply_pending;
static volatile LONG g_apply_since;  /* GetTickCount when it was asked for */

#define MENU_APPLY_WAIT 1000          /* ms: applied by then, moving or not */

/* -------------------------------------------------------- the game's thread */

static void menu_wake(void)
{
    if (g_ddraw.render.sem)
        ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
}

static void menu_push(const OVERLAY_EVENT* ev)
{
    AcquireSRWLockExclusive(&g_queue_lock);

    if (g_queue_count < MENU_QUEUE)
    {
        g_queue[(g_queue_head + g_queue_count) % MENU_QUEUE] = *ev;
        g_queue_count++;
    }

    ReleaseSRWLockExclusive(&g_queue_lock);

    menu_wake();
}

static void menu_open(void)
{
    if (!g_ddraw.hwnd)
        return;

    for (int vk = 1; vk < 256; vk++)
        g_keys_down[vk] = (real_GetAsyncKeyState(vk) & 0x8000) != 0;

    AcquireSRWLockExclusive(&g_queue_lock);
    g_queue_count = 0;
    ReleaseSRWLockExclusive(&g_queue_lock);

    g_buttons = 0;
    g_relock = g_mouse_locked;
    InterlockedExchange(&g_values_stale, 1);
    InterlockedExchange(&g_capturing, 0);

    /* Open first: mouse_lock doesn't lock while it's open */
    InterlockedExchange(&g_menu_open, 1);
    mouse_unlock();

    menu_wake();
}

void overlay_close_menu(void)
{
    if (!InterlockedExchange(&g_menu_open, 0))
        return;

    InterlockedExchange(&g_capturing, 0);

    if (g_buttons && GetCapture() == g_ddraw.hwnd)
        ReleaseCapture();

    g_buttons = 0;

    /* The keys the game saw go down and that were let go while the OSD had
       them (Alt, from Alt+F12): the game gets them going up, so none sticks.
       Not the mouse buttons, nor left and right shift, ctrl and alt, which
       come as VK_SHIFT, VK_CONTROL and VK_MENU */
    for (int vk = 1; vk < 256; vk++)
    {
        if (!g_keys_down[vk] || (real_GetAsyncKeyState(vk) & 0x8000))
            continue;

        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2 ||
            (vk >= VK_LSHIFT && vk <= VK_RMENU))
        {
            continue;
        }

        UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        PostMessageA(g_ddraw.hwnd, WM_KEYUP, vk, 1 | (scan << 16) | (1 << 30) | (1u << 31));
    }

    if (g_relock)
        mouse_lock();

    /* Nothing of the OSD is left to move */
    if (InterlockedExchange(&g_apply_pending, 0) && g_ddraw.hwnd)
        PostMessageA(g_ddraw.hwnd, WM_OVERLAY_MENU, 1, 0);

    menu_wake();
}

void overlay_toggle_menu(void)
{
    if (g_menu_open)
        overlay_close_menu();
    else
        menu_open();
}

BOOL overlay_menu_capturing(void)
{
    return g_menu_open && g_capturing;
}

/* Left and right shift, ctrl and alt told apart, for setting a hotkey */
static int menu_vk(WPARAM wParam, LPARAM lParam)
{
    BOOL extended = (lParam >> 24) & 1;

    switch (wParam)
    {
    case VK_SHIFT:
        return MapVirtualKeyA((lParam >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX);
    case VK_CONTROL:
        return extended ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU:
        return extended ? VK_RMENU : VK_LMENU;
    }

    return (int)wParam;
}

/* The characters of a key, without changing the keyboard's state (dead keys).
 * Not from WM_CHAR: not every game calls TranslateMessage */
static void menu_chars(WPARAM vk, LPARAM lParam)
{
    BYTE state[256];

    if (!GetKeyboardState(state))
        return;

    WCHAR buf[8];
    int n = ToUnicode((UINT)vk, (lParam >> 16) & 0xFF, state, buf, 8, 4);

    for (int i = 0; i < n; i++)
    {
        if (buf[i] >= 0x20 && buf[i] != 0x7F)
        {
            OVERLAY_EVENT ev = { OVERLAY_EVENT_CHAR };
            ev.ch = buf[i];
            menu_push(&ev);
        }
    }
}

static void menu_button(HWND hwnd, int button, BOOL down)
{
    OVERLAY_EVENT ev = { OVERLAY_EVENT_BUTTON };
    ev.button = button;
    ev.down = down;
    menu_push(&ev);

    /* Drags go on outside the window */
    if (down)
    {
        if (!g_buttons)
            SetCapture(hwnd);

        g_buttons |= 1 << button;
    }
    else
    {
        g_buttons &= ~(1 << button);

        if (!g_buttons && GetCapture() == hwnd)
            ReleaseCapture();
    }
}

BOOL overlay_menu_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    if (!g_menu_open)
        return FALSE;

    *result = 0;

    switch (msg)
    {
    case WM_MOUSEMOVE:
    {
        RECT rc;

        if (real_GetClientRect(hwnd, &rc) && rc.right > 0 && rc.bottom > 0)
        {
            OVERLAY_EVENT ev = { OVERLAY_EVENT_MOUSE };
            ev.x = (GET_X_LPARAM(lParam) + 0.5f) / rc.right;
            ev.y = (GET_Y_LPARAM(lParam) + 0.5f) / rc.bottom;
            menu_push(&ev);
        }

        return TRUE;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        menu_button(hwnd, 0, TRUE);
        return TRUE;
    case WM_LBUTTONUP:
        menu_button(hwnd, 0, FALSE);
        return TRUE;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        menu_button(hwnd, 1, TRUE);
        return TRUE;
    case WM_RBUTTONUP:
        menu_button(hwnd, 1, FALSE);
        return TRUE;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        menu_button(hwnd, 2, TRUE);
        return TRUE;
    case WM_MBUTTONUP:
        menu_button(hwnd, 2, FALSE);
        return TRUE;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP:
        menu_button(hwnd, GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4, msg != WM_XBUTTONUP);
        *result = TRUE;
        return TRUE;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    {
        OVERLAY_EVENT ev = { OVERLAY_EVENT_WHEEL };
        float notches = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

        if (msg == WM_MOUSEWHEEL)
            ev.y = notches;
        else
            ev.x = -notches;

        menu_push(&ev);
        return TRUE;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        /* Alt+F4 closes the game as always */
        if (wParam == VK_F4 && (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP))
            return FALSE;

        OVERLAY_EVENT ev = { OVERLAY_EVENT_KEY };
        ev.vk = menu_vk(wParam, lParam);
        ev.down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        ev.ctrl = (real_GetKeyState(VK_CONTROL) & 0x8000) != 0;
        ev.shift = (real_GetKeyState(VK_SHIFT) & 0x8000) != 0;
        ev.alt = (real_GetKeyState(VK_MENU) & 0x8000) != 0;
        menu_push(&ev);

        if (msg == WM_KEYDOWN)
            menu_chars(wParam, lParam);

        return TRUE;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
    case WM_UNICHAR:
    case WM_IME_CHAR:
        return TRUE;
    case WM_INPUT:
        *result = real_DefWindowProcA(hwnd, msg, wParam, lParam);
        return TRUE;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            real_SetCursor(LoadCursor(NULL, IDC_ARROW));
            *result = TRUE;
            return TRUE;
        }

        return FALSE;
    }

    return FALSE;
}

/* ------------------------------------------------------------- any thread */

BOOL overlay_menu_open(void)
{
    return g_menu_open != 0;
}

/* -------------------------------------------------------- the render thread */

int overlay_menu_events(OVERLAY_EVENT* events, int max)
{
    int n = 0;

    AcquireSRWLockExclusive(&g_queue_lock);

    while (n < max && g_queue_count > 0)
    {
        events[n++] = g_queue[g_queue_head];
        g_queue_head = (g_queue_head + 1) % MENU_QUEUE;
        g_queue_count--;
    }

    ReleaseSRWLockExclusive(&g_queue_lock);

    return n;
}

void overlay_menu_set_capturing(BOOL capturing)
{
    InterlockedExchange(&g_capturing, capturing ? 1 : 0);
}

void overlay_menu_request_close(void)
{
    if (g_ddraw.hwnd)
        PostMessageA(g_ddraw.hwnd, WM_OVERLAY_MENU, 0, 0);
}

float overlay_menu_font_size(int canvas_height)
{
    float size = 16.0f * overlay_dpi_scale();
    float most = canvas_height / 30.0f;

    if (size > most)
        size = most;

    if (size < 13.0f)
        size = 13.0f;

    return floorf(size);
}

static void menu_load(void)
{
    if (!g_values)
        g_values = (MENU_VALUE*)calloc(settings_count(), sizeof(MENU_VALUE));

    if (!g_values)
        return;

    INIFILE ini;
    ini_create(&ini, g_config.ini_path);

    for (int i = 0; i < settings_count(); i++)
    {
        const SETTING* s = settings_get(i);

        if (!s->key)
            continue;

        char* buf = g_values[i].value;
        ini_get_string(&ini, "directcrt", s->key, "\x01", buf, MENU_VALUE_SIZE);

        /* An empty value is as good as none, as in config.c */
        g_values[i].set = buf[0] && !(buf[0] == '\x01' && buf[1] == 0);

        if (!g_values[i].set)
            buf[0] = 0;
    }

    ini_free(&ini);
}

const char* overlay_menu_value(int setting, BOOL* set)
{
    const SETTING* s = settings_get(setting);

    if (set)
        *set = FALSE;

    if (!s || !s->key)
        return "";

    if (InterlockedExchange(&g_values_stale, 0))
        menu_load();

    if (!g_values)
        return s->value;

    MENU_VALUE* v = &g_values[setting];

    if (set)
        *set = v->set;

    return v->set ? v->value : s->value;
}

void overlay_menu_values_changed(void)
{
    InterlockedExchange(&g_values_stale, 1);
}

void overlay_menu_write(int setting, const char* value)
{
    const SETTING* s = settings_get(setting);

    if (!s || !s->key || !g_config.ini_path[0])
        return;

    /* First, so the reload timer leaves the written ini to it */
    if (!InterlockedExchange(&g_apply_pending, 1))
        InterlockedExchange(&g_apply_since, (LONG)GetTickCount());

    WritePrivateProfileStringA("directcrt", s->key, value, g_config.ini_path);

    if (g_values)
    {
        MENU_VALUE* v = &g_values[setting];

        v->set = value != NULL;
        strncpy(v->value, value ? value : "", MENU_VALUE_SIZE - 1);
        v->value[MENU_VALUE_SIZE - 1] = 0;
    }

    /* The game's thread applies it, once the OSD stands still
       (overlay_menu_frame_drawn) */
}

void overlay_menu_frame_drawn(BOOL moving)
{
    if (!g_apply_pending)
        return;

    if (moving && GetTickCount() - (DWORD)g_apply_since < MENU_APPLY_WAIT)
        return;

    if (InterlockedExchange(&g_apply_pending, 0) && g_ddraw.hwnd)
        PostMessageA(g_ddraw.hwnd, WM_OVERLAY_MENU, 1, 0);
}

BOOL overlay_menu_apply_pending(void)
{
    return g_apply_pending != 0;
}
