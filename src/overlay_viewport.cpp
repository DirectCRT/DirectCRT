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

/* A viewport's kind, as crt_auto.c reads it, and its numbers: the size, or
 * the percentages; what crt_auto.c doesn't take is "fit" */
static int menu_viewport_mode(const char* v, int* a, int* b)
{
    CRT_VIEWPORT vp;

    if (!crt_auto_parse_viewport(v, &vp))
        return CRT_VIEWPORT_FIT;

    bool percent = vp.mode == CRT_VIEWPORT_DESKTOP || vp.mode == CRT_VIEWPORT_RELATIVE;
    float scale = percent ? 100.0f : 1.0f;

    *a = (int)roundf(vp.width * scale);
    *b = vp.mode == CRT_VIEWPORT_DESKTOP ? 0 : (int)roundf(vp.height * scale);
    return vp.mode;
}

static void menu_viewport_value(int mode, int a, int b, char* buf, size_t size)
{
    switch (mode)
    {
    case CRT_VIEWPORT_LIMIT: _snprintf(buf, size - 1, "%dx%d", a, b); break;
    case CRT_VIEWPORT_LIMIT_PX: _snprintf(buf, size - 1, "%dx%dpx", a, b); break;
    case CRT_VIEWPORT_DESKTOP: _snprintf(buf, size - 1, "%d%%", a); break;
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
    static int nums[5][2] = { { 0, 0 }, { 960, 720 }, { 1440, 1080 }, { 80, 0 }, { 100, 100 } };
    static bool linked = true;
    static const char* modes[] = { "Fit", "Size", "Pixels", "Desktop", "Relative" };
    static const char* formats[] = { "fit", "WxH", "WxHpx", "N%", "relative H% V%" };

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
    int picked = ui_segmented("##mode", modes, 5, mode, F);

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
    else if (mode == CRT_VIEWPORT_DESKTOP)
    {
        int n;

        if (ui_slider_box("##desktop", "Percentage", a, 1, 100, F, &n))
        {
            write_mode = mode;
            wa = n;
        }

        _snprintf(line, sizeof(line) - 1, "%d%% of the desktop is %d\xc3\x97%d pixels", a, (int)(dw * a / 100.0f), (int)(dh * a / 100.0f));
        line[sizeof(line) - 1] = 0;
        ui_note(line, F);
    }
    else if (mode == CRT_VIEWPORT_RELATIVE)
    {
        /* The slider to 100% (inside the window or screen), the box to
         * 300%; linked, the other slider follows the one dragged */
        static int live_h = -1, live_v = -1;
        int n;
        int show_h = linked && live_v >= 0 ? live_v : a;
        int show_v = linked && live_h >= 0 ? live_h : b;

        if (ui_slider_box("##h", "Horizontal", show_h, 20, 100, F, &n, 300, &live_h))
        {
            write_mode = mode;
            wa = n;
            wb = linked ? n : b;
        }

        if (ui_slider_box("##v", "Vertical", show_v, 20, 100, F, &n, 300, &live_v))
        {
            write_mode = mode;
            wa = linked ? n : a;
            wb = n;
        }

        if (ImGui::Checkbox("Same for both", &linked) && linked && a != b)
        {
            write_mode = mode;
            wa = wb = a;
        }

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
            overlay_menu_write(setting, v);
    }
}
