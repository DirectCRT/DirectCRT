/* The settings OSD's viewport editor: each of crt-auto's viewport kinds with
 * its own controls */

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "overlay.h"
#include "overlay_icons.h"
#include "overlay_widgets.h"
#include "overlay_menu_ui.h"

extern "C" {
#include "dd.h"
#include "settings.h"
}

static DWORD g_slider_written_ms;

/* While a viewport slider is dragged: the area the viewport gives, outlined
 * behind the panel. It's the picture's layout, so it follows the writes */
static void viewport_outline(float F)
{
    const CRT_AUTO_LAYOUT* l = &g_ddraw.render.crt;

    if (l->viewport_width <= 0 || l->viewport_height <= 0)
        return;

    /* Inside the area, dark under the accent so it shows on any picture */
    float t = 0.15f * F > 1.0f ? 0.15f * F : 1.0f;
    ImVec2 a((float)l->viewport_x + t, (float)l->viewport_y + t);
    ImVec2 b((float)(l->viewport_x + l->viewport_width) - t, (float)(l->viewport_y + l->viewport_height) - t);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    dl->AddRect(a, b, IM_COL32(0, 0, 0, 160), 0.0f, t * 2.0f);
    dl->AddRect(a, b, ImGui::GetColorU32(COL_ACCENT), 0.0f, t);
}

/* A viewport's kind, as crt_auto.c reads it, and its numbers: the size, or
 * the percentages; what crt_auto.c doesn't take is "fit" */
static int menu_viewport_mode(const char* v, int* a, int* b)
{
    CRT_VIEWPORT vp;

    if (!crt_auto_parse_viewport(v, &vp))
        return CRT_VIEWPORT_FIT;

    float scale = vp.mode == CRT_VIEWPORT_RELATIVE ? 100.0f : 1.0f;

    *a = (int)roundf(vp.width * scale);
    *b = (int)roundf(vp.height * scale);
    return vp.mode;
}

static void menu_viewport_value(int mode, int a, int b, char* buf, size_t size)
{
    switch (mode)
    {
    case CRT_VIEWPORT_LIMIT: _snprintf(buf, size - 1, "%dx%d", a, b); break;
    case CRT_VIEWPORT_LIMIT_PX: _snprintf(buf, size - 1, "%dx%dpx", a, b); break;
    case CRT_VIEWPORT_RELATIVE: _snprintf(buf, size - 1, "relative %d%% %d%%", a, b); break;
    default: _snprintf(buf, size - 1, "fit"); break;
    }

    buf[size - 1] = 0;
}

/* The viewport, each of its kinds with its own controls: crt_auto.c's
 * formats without typing them */
void menu_viewport(int setting, const SETTING* s, float F)
{
    /* Each kind's numbers, by CRT_VIEWPORT_MODE, kept while another kind is tried */
    static int nums[CRT_VIEWPORT_COUNT][2] = { { 0, 0 }, { 960, 720 }, { 1440, 1080 }, { 80, 80 } };
    static bool linked = true;
    static const char* modes[CRT_VIEWPORT_COUNT] = { "Fit", "Size", "Pixels", "Relative" };
    static const char* formats[CRT_VIEWPORT_COUNT] = { "fit", "WxH", "WxHpx", "relative H% V%" };

    char value[MENU_TEXT], text[1024];
    strncpy(value, overlay_menu_value(setting, NULL), sizeof(value) - 1);
    value[sizeof(value) - 1] = 0;

    int a = 0, b = 0;
    int mode = menu_viewport_mode(value, &a, &b);

    if (mode != CRT_VIEWPORT_FIT)
    {
        nums[mode][0] = a;
        nums[mode][1] = b;
    }

    if (mode == CRT_VIEWPORT_RELATIVE && a != b)
        linked = false;

    const ImGuiStyle& style = ImGui::GetStyle();
    int write_mode = -1, wa = 0, wb = 0;
    int picked = ui_segmented("##mode", modes, CRT_VIEWPORT_COUNT, mode, F);

    if (picked >= 0 && picked != mode)
    {
        write_mode = picked;
        wa = nums[picked][0];
        wb = nums[picked][1];
    }

    if (settings_choice_help(s, formats[mode], text, sizeof(text)))
        ui_note(text, F);

    /* The desktop crt-auto fits the viewport to */
    CRT_AUTO_DISPLAY display = dd_crt_auto_display();
    int dw = display.desktop_width ? display.desktop_width : GetSystemMetrics(SM_CXSCREEN);
    int dh = display.desktop_height ? display.desktop_height : GetSystemMetrics(SM_CYSCREEN);
    float scale = display.scale;

    char line[256];

    if (mode == CRT_VIEWPORT_LIMIT || mode == CRT_VIEWPORT_LIMIT_PX)
    {
        bool px = mode == CRT_VIEWPORT_LIMIT_PX;
        int max_w = px ? dw : (int)(dw / scale), max_h = px ? dh : (int)(dh / scale);
        float field = ImGui::CalcTextSize("000000").x + style.FramePadding.x * 2;
        int n;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Width");
        ImGui::SameLine();

        if (ui_int_field("##w", a, 1, 65535, field, &n))
        {
            write_mode = mode;
            wa = n;
            wb = b;
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("Height");
        ImGui::SameLine();

        if (ui_int_field("##h", b, 1, 65535, field, &n))
        {
            write_mode = mode;
            wa = a;
            wb = n;
        }

        ImGui::SameLine();
        ImGui::TextDisabled(px ? "pixels" : "logical units");

        /* The 4:3 sizes that fit on the desktop */
        static const int presets[][2] = {
            { 640, 480 }, { 800, 600 }, { 960, 720 }, { 1024, 768 }, { 1280, 960 },
            { 1440, 1080 }, { 1600, 1200 }, { 1920, 1440 }, { 2560, 1920 }, { 2880, 2160 },
        };

        float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        bool first = true;

        for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); i++)
        {
            if (presets[i][0] > max_w || presets[i][1] > max_h)
                continue;

            _snprintf(line, sizeof(line) - 1, "%d\xc3\x97%d", presets[i][0], presets[i][1]);
            line[sizeof(line) - 1] = 0;

            float bw = ImGui::CalcTextSize(line).x + style.FramePadding.x * 2;

            if (!first)
            {
                ImGui::SameLine();

                if (ImGui::GetCursorPosX() + bw > right)
                    ImGui::NewLine();
            }

            first = false;
            ImGui::PushID((int)i);

            if (ui_button(line, bw, presets[i][0] == a && presets[i][1] == b))
            {
                write_mode = mode;
                wa = presets[i][0];
                wb = presets[i][1];
            }

            ImGui::PopID();
        }

        if (px)
            _snprintf(line, sizeof(line) - 1, "The desktop is %d\xc3\x97%d pixels", dw, dh);
        else
            _snprintf(line, sizeof(line) - 1, "The desktop is %d\xc3\x97%d logical units (%d\xc3\x97%d pixels at %d%%)",
                max_w, max_h, dw, dh, (int)(scale * 100.0f + 0.5f));

        line[sizeof(line) - 1] = 0;
        ui_note(line, F);

        if (a > max_w || b > max_h)
            ui_note("That's larger than the desktop: the window or screen is used, as with Fit", F, COL_ACCENT_HI);
    }
    else if (mode == CRT_VIEWPORT_RELATIVE)
    {
        /* Percentages of the desktop, past 100% off its edges. Locked, the
         * vertical slider is greyed and follows the horizontal one */
        static int live_h = -1, live_v = -1;
        float lock = ImGui::GetFrameHeight();
        int n;
        int cur_a = a, cur_b = b;

        if (ImGui::BeginTable("##relative", 2))
        {
            ImGui::TableSetupColumn("##sliders", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##lock", ImGuiTableColumnFlags_WidthFixed, lock);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            float top = ImGui::GetCursorScreenPos().y;

            if (ui_slider_box("##h", "Horizontal", a, 10, 200, F, &n, 0, &live_h))
            {
                write_mode = mode;
                wa = n;
                wb = linked ? n : b;
            }

            cur_a = live_h >= 0 ? live_h : a;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginDisabled(linked);

            if (ui_slider_box("##v", "Vertical", linked ? cur_a : b, 10, 200, F, &n, 0, &live_v))
            {
                write_mode = mode;
                wa = a;
                wb = n;
            }

            ImGui::EndDisabled();
            cur_b = linked ? cur_a : live_v >= 0 ? live_v : b;

            /* The padlock, between the two rows it links */
            ImGui::TableSetColumnIndex(1);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(p.x, floorf((top + p.y) * 0.5f)));

            if (ui_icon_button("##lock", linked ? ICON_LOCK : ICON_LOCK_OPEN, lock, 1.15f * F, linked))
            {
                linked = !linked;

                if (linked && a != b)
                {
                    write_mode = mode;
                    wa = wb = a;
                }
            }

            ImGui::SetItemTooltip(linked ? "Locked: the same both ways" : "Unlocked: set apart");
            ImGui::EndTable();
        }

        bool dragging = live_h >= 0 || live_v >= 0;

        if (write_mode < 0 && dragging && GetTickCount() - g_slider_written_ms >= MENU_DRAG_WRITE_MS)
        {
            write_mode = mode;
            wa = cur_a;
            wb = cur_b;
        }

        if (dragging)
            viewport_outline(F);

        _snprintf(line, sizeof(line) - 1, "Of the %d\xc3\x97%d desktop: %d\xc3\x97%d pixels", dw, dh,
            (int)(dw * cur_a / 100.0f + 0.5f), (int)(dh * cur_b / 100.0f + 0.5f));
        line[sizeof(line) - 1] = 0;
        ui_note(line, F);

        /* Another shape */
        int aspect = settings_index("aspect");

        if (a != b && aspect >= 0 && _stricmp(overlay_menu_value(aspect, NULL), "stretch"))
        {
            ui_note("The picture keeps its shape inside this area. Aspect ratio: Stretch fills it", F);

            if (ui_button("Stretch the picture", 0.0f, false))
                overlay_menu_write(aspect, "stretch");
        }
    }

    if (write_mode >= 0)
    {
        char v[64];
        menu_viewport_value(write_mode, wa, wb, v, sizeof(v));

        if (strcmp(v, value))
        {
            overlay_menu_write(setting, v);
            g_slider_written_ms = GetTickCount();
        }
    }
}

/* ---------------------------------------- viewport_offset: moving the picture */

static int g_pan = -1;              /* the setting being moved, or -1 */
static char g_pan_start[MENU_TEXT]; /* its value before, for Cancel */
static bool g_pan_start_set;        /* and whether the ini had it */
static float g_pan_x, g_pan_y;      /* where it is, as crt_auto.c reads it */
static bool g_pan_dragging;
static bool g_pan_unwritten;        /* dragged since the last write */
static DWORD g_pan_written_ms;

static float pan_clamp(float v)
{
    return v < -CRT_AUTO_OFFSET_MAX ? -CRT_AUTO_OFFSET_MAX : v > CRT_AUTO_OFFSET_MAX ? CRT_AUTO_OFFSET_MAX : v;
}

/* A fraction as a percentage, without the zeros it doesn't need */
static void pan_percent(float v, char* buf, size_t size)
{
    _snprintf(buf, size - 1, "%.4f", v * 100.0f);
    buf[size - 1] = 0;

    char* end = buf + strlen(buf);

    while (end > buf && end[-1] == '0')
        *--end = 0;

    if (end > buf && end[-1] == '.')
        *--end = 0;

    if (!strcmp(buf, "-0"))
        strncpy(buf, "0", size);
}

static void pan_write(void)
{
    char x[32], y[32], v[64];
    pan_percent(g_pan_x, x, sizeof(x));
    pan_percent(g_pan_y, y, sizeof(y));
    _snprintf(v, sizeof(v) - 1, "%s%% %s%%", x, y);
    v[sizeof(v) - 1] = 0;

    overlay_menu_write(g_pan, v);
    g_pan_written_ms = GetTickCount();
    g_pan_unwritten = false;
}

static void pan_move(float dx, float dy)
{
    g_pan_x = pan_clamp(g_pan_x + dx);
    g_pan_y = pan_clamp(g_pan_y + dy);
}

/* One of n: a step of one pixel of the game's picture or of the screen's */
static float pan_step(int n)
{
    return n > 0 ? 1.0f / n : 0.001f;
}

/* "Right 20 px, up 4 px", in the screen's pixels */
static void pan_describe(float x, float y, char* buf, size_t size)
{
    int px = (int)roundf(x * g_ddraw.render.viewport.width);
    int py = (int)roundf(y * g_ddraw.render.viewport.height);

    if (px == 0 && py == 0)
        strncpy(buf, x == 0.0f && y == 0.0f ? "Centred" : "Less than a pixel from the centre", size - 1);
    else if (py == 0)
        _snprintf(buf, size - 1, "%s %d px", px > 0 ? "Right" : "Left", abs(px));
    else if (px == 0)
        _snprintf(buf, size - 1, "%s %d px", py > 0 ? "Down" : "Up", abs(py));
    else
        _snprintf(buf, size - 1, "%s %d px, %s %d px", px > 0 ? "Right" : "Left", abs(px), py > 0 ? "down" : "up", abs(py));

    buf[size - 1] = 0;
}

bool menu_pan_active(void)
{
    return g_pan >= 0;
}

static void menu_pan_start(int setting)
{
    BOOL set = FALSE;
    const char* v = overlay_menu_value(setting, &set);

    strncpy(g_pan_start, v, sizeof(g_pan_start) - 1);
    g_pan_start[sizeof(g_pan_start) - 1] = 0;
    g_pan_start_set = set != FALSE;

    g_pan_x = g_pan_y = 0.0f;
    crt_auto_parse_offset(v, &g_pan_x, &g_pan_y);

    g_pan = setting;
    g_pan_dragging = false;
    g_pan_unwritten = false;

    /* The keys are the picture's now: ImGui won't see the Enter that started
     * it let go */
    ImGui::GetIO().ClearInputKeys();
}

void menu_pan_end(bool keep)
{
    if (g_pan < 0)
        return;

    if (!keep)
        overlay_menu_write(g_pan, g_pan_start_set ? g_pan_start : NULL);
    else if (g_pan_unwritten)
        pan_write();

    g_pan = -1;
    g_pan_dragging = false;
    ImGui::GetIO().ClearInputKeys();
}

void menu_pan_key(const OVERLAY_EVENT* e)
{
    if (g_pan < 0 || !e->down)
        return;

    /* A pixel of the game's picture; ten with Shift; one of the screen's with Ctrl */
    float sx = pan_step(g_ddraw.width), sy = pan_step(g_ddraw.height);

    if (e->ctrl)
    {
        sx = pan_step(g_ddraw.render.viewport.width);
        sy = pan_step(g_ddraw.render.viewport.height);
    }
    else if (e->shift)
    {
        sx *= 10.0f;
        sy *= 10.0f;
    }

    switch (e->vk)
    {
    case VK_LEFT: pan_move(-sx, 0.0f); break;
    case VK_RIGHT: pan_move(sx, 0.0f); break;
    case VK_UP: pan_move(0.0f, -sy); break;
    case VK_DOWN: pan_move(0.0f, sy); break;
    case VK_HOME: g_pan_x = g_pan_y = 0.0f; break;
    case VK_RETURN: menu_pan_end(true); return;
    case VK_ESCAPE: menu_pan_end(false); return;
    default: return;
    }

    pan_write();
}

/* While the picture is moved: no panel in front of it, a bar at the bottom
 * with the keys, and the mouse drags it */
void menu_pan_draw(int width, int canvas_height, float F)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiStyle& style = ImGui::GetStyle();
    float pw = (float)g_ddraw.render.viewport.width, ph = (float)g_ddraw.render.viewport.height;
    bool over_bar = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !over_bar)
        g_pan_dragging = true;

    if (!over_bar || g_pan_dragging)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    if (g_pan_dragging)
    {
        if ((io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) && pw > 0.0f && ph > 0.0f)
        {
            pan_move(io.MouseDelta.x / pw, io.MouseDelta.y / ph);
            g_pan_unwritten = true;
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            g_pan_dragging = false;

        if (g_pan_unwritten && (!g_pan_dragging || GetTickCount() - g_pan_written_ms >= MENU_DRAG_WRITE_MS))
            pan_write();
    }

    static const char* const hints[][2] = {
        { "\xe2\x86\x90\xe2\x86\x91\xe2\x86\x92\xe2\x86\x93", "Move" },
        { "Shift", "10\xc3\x97" },
        { "Ctrl", "1 pixel" },
        { "Home", "Centre" },
        { "Enter", "Keep" },
        { "Esc", "Cancel" },
    };
    const int count = (int)(sizeof(hints) / sizeof(hints[0]));

    /* The hints' width, measured right to left as they're drawn */
    float hx = 0.0f;

    for (int i = count - 1; i >= 0; i--)
        hx = menu_hint(NULL, hx, 0.0f, -FLT_MAX, hints[i][0], hints[i][1], F);

    float hints_w = -hx - 1.2f * F;
    float pad = 0.9f * F;

    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, canvas_height - 1.5f * F), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.4f * F);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;

    bool shown = ImGui::Begin("##pan", NULL, flags);
    ImGui::PopStyleVar(2);

    if (shown)
    {
        char line[160];

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Move the picture");

        float cw = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2;
        float kw = ImGui::CalcTextSize("Keep").x + style.FramePadding.x * 2;
        float bx = pad + hints_w - cw - style.ItemSpacing.x - kw;

        ImGui::SameLine();

        if (bx > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(bx);

        if (ui_button("Cancel", cw, false))
            menu_pan_end(false);

        ImGui::SameLine();

        if (ui_button("Keep", kw, true))
            menu_pan_end(true);

        pan_describe(g_pan_x, g_pan_y, line, sizeof(line));
        strncat(line, ". Or drag it with the mouse", sizeof(line) - strlen(line) - 1);
        ui_note(line, F);

        ImGui::Dummy(ImVec2(hints_w, 2.0f * F));
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        hx = b.x;

        for (int i = count - 1; i >= 0 && hx >= 0.0f; i--)
            hx = menu_hint(dl, hx, (a.y + b.y) * 0.5f, a.x - 1.0f, hints[i][0], hints[i][1], F);
    }

    ImGui::End();
}

/* The viewport_offset row: where the picture is, and the button that moves it */
void menu_viewport_offset(int setting, const SETTING* s, float F)
{
    (void)s;

    float x = 0.0f, y = 0.0f;
    char line[160];

    crt_auto_parse_offset(overlay_menu_value(setting, NULL), &x, &y);
    pan_describe(x, y, line, sizeof(line));
    ImGui::TextUnformatted(line);

    if (ui_button("Move the picture", 0.0f, true))
        menu_pan_start(setting);

    ui_take_focus(ImGui::GetItemID());
}
