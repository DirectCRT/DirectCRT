#include <windows.h>
#include "vbeam.h"


static LONGLONG g_freq;     /* counter ticks per second */
static LONGLONG g_origin;   /* counter value at the start of refresh 0 */
static volatile LONG g_hz = 60;
static volatile LONG g_lines = VBEAM_VGA_ACTIVE_LINES;
static CRITICAL_SECTION g_cs; /* g_flip_due; held only to read or set it, never while waiting */
static volatile LONG g_high_res = -1; /* whether high-resolution timers work; -1 = not tried yet */
static LONGLONG g_flip_due; /* when the pending flip is shown; 0 if none */

/* Each waiting thread's own timer, so threads can wait at once; made at its
   first wait, not every frame, and closed by vbeam_thread_exit */
static __declspec(thread) HANDLE g_timer;
static __declspec(thread) BOOL g_timer_tried;

LONGLONG vbeam_next_begin(LONGLONG t, LONGLONG period, LONGLONG active)
{
    if (t < active)
        return active;

    return ((t - active) / period + 1) * period + active;
}

LONGLONG vbeam_next_end(LONGLONG t, LONGLONG period)
{
    return (t / period + 1) * period;
}

DWORD vbeam_line_at(LONGLONG t, LONGLONG period, DWORD total_lines)
{
    return (DWORD)((t % period) * total_lines / period);
}

DWORD vbeam_total_lines(DWORD active_lines)
{
    if (!active_lines)
        active_lines = VBEAM_VGA_ACTIVE_LINES;

    return (active_lines * VBEAM_VGA_TOTAL_LINES + VBEAM_VGA_ACTIVE_LINES / 2) / VBEAM_VGA_ACTIVE_LINES;
}

void vbeam_init(void)
{
    LARGE_INTEGER f, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&now);

    g_freq = f.QuadPart;
    g_origin = now.QuadPart;
    InitializeCriticalSection(&g_cs);
}

void vbeam_set_rate(DWORD hz)
{
    if (hz >= VBEAM_MIN_HZ && hz <= VBEAM_MAX_HZ)
        InterlockedExchange(&g_hz, (LONG)hz);
}

DWORD vbeam_rate(void)
{
    return (DWORD)g_hz;
}

void vbeam_set_lines(DWORD active_lines)
{
    if (active_lines)
        InterlockedExchange(&g_lines, (LONG)active_lines);
}

static LONGLONG period(void)
{
    return g_freq / g_hz;
}

static DWORD active_lines(void)
{
    return (DWORD)g_lines;
}

/* The counter ticks of one refresh the beam spends drawing lines. */
static LONGLONG active_part(LONGLONG p)
{
    DWORD active = active_lines();
    return p * active / vbeam_total_lines(active);
}

static LONGLONG now(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart - g_origin;
}

/* A high-resolution timer wakes within about half a millisecond; Windows
   before 10 1803 refuses the flag, and then an ordinary timer is used */
static HANDLE create_timer(void)
{
    typedef HANDLE (WINAPI *CREATETIMEREXW)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
    HANDLE h = NULL;

    if (g_high_res != 0)
    {
        CREATETIMEREXW create_ex =
            (CREATETIMEREXW)(void*)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "CreateWaitableTimerExW");

        if (create_ex)
            h = create_ex(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

        InterlockedExchange(&g_high_res, h != NULL);
    }

    if (!h)
        h = CreateWaitableTimer(NULL, FALSE, NULL);

    return h;
}

/* Never returns before t. Call without g_cs held */
static void wait_until(LONGLONG t)
{
    for (;;)
    {
        LONGLONG left = t - now();

        if (left <= 0)
            break;

        LONGLONG left_100ns = left * 10000000 / g_freq;

        if (left_100ns > 2000 && !g_timer_tried)
        {
            g_timer = create_timer();
            g_timer_tried = TRUE;
        }

        if (left_100ns > 2000 && g_timer)
        {
            LARGE_INTEGER due = { 0 };
            due.QuadPart = -left_100ns;

            if (SetWaitableTimer(g_timer, &due, 0, NULL, NULL, FALSE))
            {
                WaitForSingleObject(g_timer, (DWORD)(left_100ns / 10000) + 5);
                continue;
            }
        }

        if (left_100ns > 20000)
            Sleep(1);
        else
            SwitchToThread();
    }
}

void vbeam_thread_exit(void)
{
    if (g_timer)
        CloseHandle(g_timer);

    g_timer = NULL;
    g_timer_tried = FALSE;
}

static LONGLONG flip_due(void)
{
    EnterCriticalSection(&g_cs);
    LONGLONG due = g_flip_due;
    LeaveCriticalSection(&g_cs);
    return due;
}

BOOL vbeam_in_vblank(void)
{
    BOOL in_vblank;
    vbeam_scanline(&in_vblank);
    return in_vblank;
}

DWORD vbeam_scanline(BOOL* in_vblank)
{
    DWORD active = active_lines();
    DWORD line = vbeam_line_at(now(), period(), vbeam_total_lines(active));

    if (in_vblank)
        *in_vblank = line >= active;

    return line;
}

void vbeam_wait(BOOL end)
{
    /* Each thread waits for the edge after its own call, so threads waiting
       together all wake at the same one */
    LONGLONG p = period();
    LONGLONG t = now();
    wait_until(end ? vbeam_next_end(t, p) : vbeam_next_begin(t, p, active_part(p)));
}

BOOL vbeam_flip_pending(void)
{
    LONGLONG due = flip_due();
    return due && now() < due;
}

void vbeam_wait_flip_shown(void)
{
    LONGLONG due = flip_due();

    if (due)
        wait_until(due);
}

void vbeam_flip(void)
{
    LONGLONG due = flip_due();

    if (due)
        wait_until(due);

    LONGLONG p = period();
    LONGLONG next = vbeam_next_begin(now(), p, active_part(p));

    EnterCriticalSection(&g_cs);
    g_flip_due = next;
    LeaveCriticalSection(&g_cs);
}
