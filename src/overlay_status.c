#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "dd.h"
#include "config.h"
#include "crt_auto.h"
#include "render_d3d11.h"
#include "render_ogl.h"
#include "overlay.h"
#include "vbeam.h"
#include "hook.h"

/* The status OSD's state and numbers. The hotkey runs on the game's thread,
 * everything else on the render thread */

#define OVERLAY_PERIOD_MS 500.0  /* the numbers change twice a second */
#define OVERLAY_REFRESH_MS 250   /* the longest wait for the game while shown */
#define OVERLAY_MENU_MS 16       /* the same, while the settings OSD is open */

static volatile LONG g_overlay_visible;
static volatile LONG g_overlay_reset;

static struct
{
    LARGE_INTEGER freq;
    LONGLONG period_start;
    LONGLONG last_new;

    /* this period */
    int presents;
    int frames;
    int intervals;
    double frame_ms_sum;
    double frame_ms_max;
    double gpu_ms_sum;
    int gpu_count;

    /* shown: the last full period */
    BOOL ready;
    double fps;
    double presented_fps;
    double frame_ms;
    double frame_ms_max_shown;
    double gpu_ms;
    BOOL have_gpu;

    /* the time between the game's pictures */
    float history[OVERLAY_HISTORY];
    int head;
    int count;
} g_stats;

void overlay_toggle_status(void)
{
    if (InterlockedXor(&g_overlay_visible, 1) == 0)
        InterlockedExchange(&g_overlay_reset, 1);

    /* Draw now, even if the game is waiting for input */
    if (g_ddraw.render.sem)
        ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
}

BOOL overlay_status_visible(void)
{
    return g_overlay_visible != 0;
}

BOOL overlay_visible(void)
{
    return g_overlay_visible != 0 || overlay_menu_open();
}

DWORD overlay_wait_timeout(DWORD timeout)
{
    /* The settings OSD answers the mouse at about 60 frames a second */
    if (overlay_menu_open() && timeout > OVERLAY_MENU_MS)
        return OVERLAY_MENU_MS;

    if (g_overlay_visible && timeout > OVERLAY_REFRESH_MS)
        return OVERLAY_REFRESH_MS;

    return timeout;
}

static double overlay_ms(LONGLONG ticks)
{
    return (double)ticks * 1000.0 / (double)g_stats.freq.QuadPart;
}

void overlay_frame_presented(BOOL new_frame)
{
    if (!g_overlay_visible)
        return;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (InterlockedExchange(&g_overlay_reset, 0) || !g_stats.freq.QuadPart)
    {
        memset(&g_stats, 0, sizeof(g_stats));
        QueryPerformanceFrequency(&g_stats.freq);
        g_stats.period_start = now.QuadPart;
    }

    g_stats.presents++;

    if (new_frame)
    {
        if (g_stats.last_new)
        {
            double ms = overlay_ms(now.QuadPart - g_stats.last_new);

            g_stats.frame_ms_sum += ms;
            g_stats.intervals++;

            if (ms > g_stats.frame_ms_max)
                g_stats.frame_ms_max = ms;

            g_stats.history[g_stats.head] = (float)ms;
            g_stats.head = (g_stats.head + 1) % OVERLAY_HISTORY;

            if (g_stats.count < OVERLAY_HISTORY)
                g_stats.count++;
        }

        g_stats.frames++;
        g_stats.last_new = now.QuadPart;
    }

    double period = overlay_ms(now.QuadPart - g_stats.period_start);

    if (period >= OVERLAY_PERIOD_MS)
    {
        g_stats.ready = TRUE;
        g_stats.fps = g_stats.frames * 1000.0 / period;
        g_stats.presented_fps = g_stats.presents * 1000.0 / period;

        g_stats.frame_ms = g_stats.intervals ? g_stats.frame_ms_sum / g_stats.intervals : 0;
        g_stats.frame_ms_max_shown = g_stats.frame_ms_max;
        g_stats.have_gpu = g_stats.gpu_count > 0;
        g_stats.gpu_ms = g_stats.have_gpu ? g_stats.gpu_ms_sum / g_stats.gpu_count : 0;

        g_stats.period_start = now.QuadPart;
        g_stats.presents = 0;
        g_stats.frames = 0;
        g_stats.intervals = 0;
        g_stats.frame_ms_sum = 0;
        g_stats.frame_ms_max = 0;
        g_stats.gpu_ms_sum = 0;
        g_stats.gpu_count = 0;
    }
}

void overlay_gpu_time(double ms)
{
    if (!g_overlay_visible)
        return;

    g_stats.gpu_ms_sum += ms;
    g_stats.gpu_count++;
}

/* ------------------------------------------------------------------ text */

static void overlay_row(OVERLAY_STATUS* status, const char* label, const char* fmt, ...)
{
    if (status->rows >= OVERLAY_MAX_ROWS)
        return;

    strncpy(status->row[status->rows].label, label, sizeof(status->row[0].label) - 1);
    status->row[status->rows].label[sizeof(status->row[0].label) - 1] = 0;

    va_list args;
    va_start(args, fmt);
    _vsnprintf(status->row[status->rows].value, sizeof(status->row[0].value) - 1, fmt, args);
    va_end(args);

    status->row[status->rows].value[sizeof(status->row[0].value) - 1] = 0;
    status->rows++;
}

/* "4.50x", or "5.00x6.00x" when the picture is wider or narrower than the
 * game's pixels (aspect correction) */
static void overlay_scale(char* buf, size_t size)
{
    double sx = g_ddraw.width ? (double)g_ddraw.render.viewport.width / g_ddraw.width : 0;
    double sy = g_ddraw.height ? (double)g_ddraw.render.viewport.height / g_ddraw.height : 0;

    if (sx - sy < 0.005 && sy - sx < 0.005)
        _snprintf(buf, size - 1, "%.2fx", sy);
    else
        _snprintf(buf, size - 1, "%.2fx horizontal, %.2fx vertical", sx, sy);

    buf[size - 1] = 0;
}

static const char* overlay_renderer_name(void)
{
    if (g_ddraw.renderer == d3d11_render_main)
        return d3d11_is_warp() ? "Direct3D 11 (WARP)" : "Direct3D 11";

    if (g_ddraw.renderer == ogl_render_main)
        return "OpenGL";

    return "none";
}

static const char* overlay_mode_name(void)
{
    if (g_config.windowed && !g_config.fullscreen)
        return "window";

    if (!g_config.windowed && g_config.fullscreen)
        return "fullscreen";

    if (!g_config.windowed && !g_config.fullscreen)
        return "original";

    return "original, borderless";
}

static const char* overlay_shader_setting(void)
{
    switch (g_config.crt.shader)
    {
    case CRT_SHADER_SETTING_CRT_AUTO: return "crt-auto";
    case CRT_SHADER_SETTING_SHARP: return "sharp";
    case CRT_SHADER_SETTING_NEAREST: return "nearest";
    case CRT_SHADER_SETTING_BILINEAR: return "bilinear";
    }

    return "?";
}

/* The emulated display's rate and where it comes from */
static void overlay_refresh(char* buf, size_t size)
{
    DWORD hz = vbeam_rate();

    if (g_config.emulated_refresh)
        _snprintf(buf, size - 1, "%lu Hz, from emulated_refresh", hz);
    else if (g_ddraw.refresh_asked == hz)
        _snprintf(buf, size - 1, "%lu Hz, the game's choice", hz);
    else if (g_ddraw.refresh_asked)
        _snprintf(buf, size - 1, "%lu Hz, the game asked for %lu", hz, g_ddraw.refresh_asked);
    else
        _snprintf(buf, size - 1, "%lu Hz, the game didn't ask", hz);

    buf[size - 1] = 0;
}

/* The refresh rate of the monitor the window is on, looked up twice a second;
   0 if Windows doesn't say */
static DWORD overlay_monitor_hz(void)
{
    static DWORD hz, looked_at;
    static BOOL looked;
    DWORD now = GetTickCount();

    if (looked && now - looked_at < (DWORD)OVERLAY_PERIOD_MS)
        return hz;

    looked = TRUE;
    looked_at = now;
    hz = 0;

    MONITORINFOEXA mi = { 0 };
    mi.cbSize = sizeof(mi);

    HMONITOR monitor = MonitorFromWindow(g_ddraw.hwnd, MONITOR_DEFAULTTONEAREST);

    DEVMODEA m = { 0 };
    m.dmSize = sizeof(m);

    if (monitor && GetMonitorInfoA(monitor, (MONITORINFO*)&mi) &&
        real_EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &m) &&
        m.dmDisplayFrequency > 1) /* 0 and 1 mean the hardware's default */
    {
        hz = m.dmDisplayFrequency;
    }

    return hz;
}

/* What the game's waits for the vertical blank and its flips follow
   (maxgameticks) */
static void overlay_vblank(char* buf, size_t size)
{
    int ticks = g_config.maxgameticks;

    if (ticks == 0)
        _snprintf(buf, size - 1, "emulated, flips are shown at it");
    else if (ticks == -2)
        _snprintf(buf, size - 1, "your monitor's");
    else if (ticks < 0)
        _snprintf(buf, size - 1, "off, waiting for it returns at once");
    else
        _snprintf(buf, size - 1, "emulated, flips don't wait; game speed %d ticks/s", ticks);

    buf[size - 1] = 0;
}

float overlay_dpi_scale(void)
{
    typedef UINT(WINAPI* GETDPIFORWINDOWPROC)(HWND);
    static GETDPIFORWINDOWPROC get_dpi_for_window;
    static BOOL looked;

    if (!looked)
    {
        get_dpi_for_window =
            (GETDPIFORWINDOWPROC)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
        looked = TRUE;
    }

    UINT dpi = get_dpi_for_window && g_ddraw.hwnd ? get_dpi_for_window(g_ddraw.hwnd) : 0;

    return dpi ? dpi / 96.0f : 1.0f;
}

void overlay_get_status(OVERLAY_STATUS* status, int canvas_width, int canvas_height)
{
    static const float sizes[] = { 13.0f, 16.0f, 20.0f };

    memset(status, 0, sizeof(*status));

    int size = g_config.status_osd_size;
    if (size < 0 || size > 2)
        size = 1;

    /* For the display scale, but never more than a 22nd of the picture's height */
    float font = sizes[size] * overlay_dpi_scale();

    if (font > canvas_height / 22.0f)
        font = canvas_height / 22.0f;

    if (font < 10.0f)
        font = 10.0f;

    status->font_size = font;
    status->scale = font / 13.0f;
    status->corner = (OVERLAY_CORNER)g_config.status_osd_position;

    char scale[64];
    overlay_scale(scale, sizeof(scale));

    const char* shader = crt_auto_shader_name(g_ddraw.render.crt.shader);

    char fps[32] = "-", frame[64] = "-", gpu[32] = "-";

    if (g_stats.ready)
    {
        _snprintf(fps, sizeof(fps) - 1, "%.0f fps", g_stats.fps);

        if (g_stats.frame_ms > 0)
            _snprintf(frame, sizeof(frame) - 1, "%.1f ms", g_stats.frame_ms);

        if (g_stats.have_gpu)
            _snprintf(gpu, sizeof(gpu) - 1, "%.2f ms", g_stats.gpu_ms);
    }

    if (!g_config.status_osd_full)
    {
        overlay_row(
            status, "", "%dx%d %d-bit %lu Hz -> %dx%d, %s",
            g_ddraw.width, g_ddraw.height, g_ddraw.bpp, vbeam_rate(),
            g_ddraw.render.viewport.width, g_ddraw.render.viewport.height, scale);

        overlay_row(status, "", "%s, %s", shader, overlay_renderer_name());

        if (g_stats.ready && g_stats.frame_ms > 0)
            overlay_row(status, "", "%s, %s", fps, frame);
        else
            overlay_row(status, "", "%s", fps);

        return;
    }

    overlay_row(
        status, "Game", "%dx%d, %d-bit, %s", g_ddraw.width, g_ddraw.height, g_ddraw.bpp, overlay_mode_name());

    overlay_row(
        status, g_config.windowed && !g_config.fullscreen ? "Window" : "Screen", "%dx%d", canvas_width, canvas_height);

    overlay_row(
        status, "Picture", "%dx%d at %d,%d",
        g_ddraw.render.viewport.width, g_ddraw.render.viewport.height,
        g_ddraw.render.viewport.x, g_ddraw.render.viewport.y);

    overlay_row(
        status, "Scale", "%s%s%s", scale,
        g_ddraw.render.crt.double_scanned ? ", double scanned" : "",
        g_ddraw.render.crt.pixel_doubled ? ", pixels doubled" : "");

    if (g_config.crt.shader == CRT_SHADER_SETTING_CRT_AUTO)
    {
        overlay_row(
            status, "Shader", "crt-auto: %s, %d px per line", shader, g_ddraw.render.crt.pixels_per_scanline);
    }
    else
    {
        overlay_row(status, "Shader", "%s", overlay_shader_setting());
    }

    overlay_row(status, "Renderer", "%s, vsync %s", overlay_renderer_name(), g_config.vsync ? "on" : "off");

    char refresh[64], vblank[64];
    overlay_refresh(refresh, sizeof(refresh));
    overlay_vblank(vblank, sizeof(vblank));

    DWORD monitor_hz = overlay_monitor_hz();

    if (monitor_hz)
        overlay_row(status, "Refresh", "%s; monitor %lu Hz", refresh, monitor_hz);
    else
        overlay_row(status, "Refresh", "%s", refresh);

    overlay_row(status, "Vertical blank", "%s", vblank);

    if (g_stats.ready)
    {
        overlay_row(status, "Frame rate", "%s (%.0f presented)", fps, g_stats.presented_fps);

        if (g_stats.frame_ms > 0)
            overlay_row(status, "Frame time", "%.1f ms, max %.1f ms", g_stats.frame_ms, g_stats.frame_ms_max_shown);
        else
            overlay_row(status, "Frame time", "-");
    }
    else
    {
        overlay_row(status, "Frame rate", "-");
        overlay_row(status, "Frame time", "-");
    }

    overlay_row(status, "GPU time", "%s", gpu);

    /* The graph, oldest first, on a scale of 25, 50, 100 or 250 ms */
    status->graph = TRUE;
    status->frame_count = g_stats.count;

    float max = 0;

    for (int i = 0; i < g_stats.count; i++)
    {
        int at = (g_stats.head - g_stats.count + i + OVERLAY_HISTORY) % OVERLAY_HISTORY;
        status->frame_ms[i] = g_stats.history[at];

        if (g_stats.history[at] > max)
            max = g_stats.history[at];
    }

    /* the slowest frame at most 80% up, clear of the scale's text */
    status->graph_max_ms = max <= 20.0f ? 25.0f : max <= 40.0f ? 50.0f : max <= 80.0f ? 100.0f : 250.0f;
}
