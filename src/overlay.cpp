/* The status OSD and the settings OSD, drawn with Dear ImGui (third_party/imgui)
 * over the finished frame. There's no platform backend: the display size and
 * the time step are set here, and the settings OSD's input comes from
 * overlay_menu.c's queue. The ImGui context lives as long as the process; the
 * renderer backend is set up the first time the OSD is drawn and shut down
 * with the renderer's resources. */

#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_opengl3.h"
#include "overlay.h"

extern "C" {
#include "settings.h"
#include "display_settings.h"
}

enum OVERLAY_BACKEND
{
    OVERLAY_NONE,
    OVERLAY_DX11,
    OVERLAY_OGL
};

static ImGuiContext* g_ctx;
static ImFont* g_font;
static ImGuiStyle g_base_style;
static float g_style_scale;
static LARGE_INTEGER g_last_frame;

static OVERLAY_BACKEND g_backend;
static ID3D11Device* g_dx_device;  /* the device the backend was set up with */
static bool g_dx_failed;
static bool g_ogl_failed;

static void overlay_init_context()
{
    if (g_ctx)
        return;

    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    /* ProggyForever, scalable, with monospaced digits */
    g_font = io.Fonts->AddFontDefaultVector();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    g_base_style = style;
    g_style_scale = 1.0f;
}

/* ---------------------------------------------------------- the status OSD */

static void overlay_draw_status(const OVERLAY_STATUS& status, int width, int canvas_height)
{
    float margin = 8.0f * status.scale;
    bool right = status.corner == OVERLAY_TOP_RIGHT || status.corner == OVERLAY_BOTTOM_RIGHT;
    bool bottom = status.corner == OVERLAY_BOTTOM_LEFT || status.corner == OVERLAY_BOTTOM_RIGHT;

    ImGui::SetNextWindowPos(
        ImVec2(right ? width - margin : margin, bottom ? canvas_height - margin : margin),
        ImGuiCond_Always,
        ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));

    ImGui::SetNextWindowBgAlpha(0.75f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##status", NULL, flags))
    {
        if (!status.graph)
        {
            for (int i = 0; i < status.rows; i++)
                ImGui::TextUnformatted(status.row[i].value);
        }
        else
        {
            float graph_width = OVERLAY_HISTORY * status.scale;

            if (ImGui::BeginTable("##rows", 2, ImGuiTableFlags_SizingFixedFit))
            {
                for (int i = 0; i < status.rows; i++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "%s", status.row[i].label);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(status.row[i].value);
                }

                ImGui::EndTable();

                /* as wide as the table */
                if (ImGui::GetItemRectSize().x > graph_width)
                    graph_width = ImGui::GetItemRectSize().x;
            }

            char scale[32];
            _snprintf(scale, sizeof(scale) - 1, "0-%.0f ms", status.graph_max_ms);
            scale[sizeof(scale) - 1] = 0;

            ImGui::PlotLines(
                "##frames",
                status.frame_ms,
                status.frame_count,
                0,
                scale,
                0.0f,
                status.graph_max_ms,
                ImVec2(graph_width, 48.0f * status.scale));
        }
    }

    ImGui::End();
}

/* -------------------------------------------------------- the settings OSD */

#define MENU_TEXT 264

static bool g_menu_drawn;      /* the settings OSD was in the last frame */
static bool g_scope_game;      /* editing the game's own section */
static int g_help = -1;        /* the setting the help pane is about */
static int g_capture = -1;     /* the hotkey being set */
static bool g_capture_seen;    /* its button was drawn this frame */
static int g_slider = -1;      /* the slider being dragged, and its value */
static int g_slider_value;
static int g_text = -1;        /* the text being edited, and its text */
static char g_text_value[MENU_TEXT];
static int g_custom = -1;      /* the text setting whose "Other" was picked */
static bool g_custom_focus;
static bool g_key_down[256];   /* the keys ImGui saw pressed */

static void menu_capture(int setting)
{
    g_capture = setting;
    g_capture_seen = setting >= 0; /* its button is on screen: it was just clicked */
    overlay_menu_set_capturing(setting >= 0);
}

static void menu_reset(void)
{
    menu_capture(-1);
    g_slider = -1;
    g_text = -1;
    g_custom = -1;
}

static ImGuiKey menu_imgui_key(int vk)
{
    if (vk >= '0' && vk <= '9')
        return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));

    if (vk >= 'A' && vk <= 'Z')
        return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));

    if (vk >= VK_F1 && vk <= VK_F24)
        return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));

    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return (ImGuiKey)(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));

    switch (vk)
    {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_OEM_7: return ImGuiKey_Apostrophe;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_2: return ImGuiKey_Slash;
    case VK_OEM_1: return ImGuiKey_Semicolon;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_4: return ImGuiKey_LeftBracket;
    case VK_OEM_5: return ImGuiKey_Backslash;
    case VK_OEM_6: return ImGuiKey_RightBracket;
    case VK_OEM_3: return ImGuiKey_GraveAccent;
    case VK_OEM_102: return ImGuiKey_Oem102;
    case VK_CAPITAL: return ImGuiKey_CapsLock;
    case VK_SCROLL: return ImGuiKey_ScrollLock;
    case VK_NUMLOCK: return ImGuiKey_NumLock;
    case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
    case VK_PAUSE: return ImGuiKey_Pause;
    case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE: return ImGuiKey_KeypadDivide;
    case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_RMENU: return ImGuiKey_RightAlt;
    case VK_LWIN: return ImGuiKey_LeftSuper;
    case VK_RWIN: return ImGuiKey_RightSuper;
    case VK_APPS: return ImGuiKey_Menu;
    }

    return ImGuiKey_None;
}

static bool menu_is_modifier(int vk)
{
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || (vk >= VK_LSHIFT && vk <= VK_RMENU);
}

/* A key for the hotkey being set. The keyboard hook sees Shift, Ctrl and Alt
 * without left and right, so only the unlock keys (read with GetAsyncKeyState)
 * keep them apart. A modifier or Print Screen (which has no key down) counts
 * when it's let go, any other key when it's pressed */
static void menu_capture_key(const OVERLAY_EVENT* e)
{
    int vk = e->vk;

    if (vk == VK_ESCAPE)
    {
        if (e->down)
            menu_capture(-1);

        return;
    }

    bool on_release = menu_is_modifier(vk) || vk == VK_SNAPSHOT;

    if ((e->down != 0) == on_release)
        return;

    const SETTING* s = settings_get(g_capture);

    if (!s || !s->key)
    {
        menu_capture(-1);
        return;
    }

    if (menu_is_modifier(vk) && strcmp(s->key, "keyunlockcursor1") && strcmp(s->key, "keyunlockcursor2"))
    {
        if (vk == VK_LSHIFT || vk == VK_RSHIFT)
            vk = VK_SHIFT;
        else if (vk == VK_LCONTROL || vk == VK_RCONTROL)
            vk = VK_CONTROL;
        else if (vk == VK_LMENU || vk == VK_RMENU)
            vk = VK_MENU;
    }

    char value[8];
    _snprintf(value, sizeof(value) - 1, "0x%02X", vk & 0xFF);
    value[sizeof(value) - 1] = 0;

    overlay_menu_write(g_capture, g_scope_game, value);
    menu_capture(-1);
}

/* The queued input into ImGui */
static void menu_input(int width, int canvas_height)
{
    ImGuiIO& io = ImGui::GetIO();
    OVERLAY_EVENT events[64];
    int n;

    while ((n = overlay_menu_events(events, 64)) > 0)
    {
        for (int i = 0; i < n; i++)
        {
            const OVERLAY_EVENT* e = &events[i];

            switch (e->type)
            {
            case OVERLAY_EVENT_MOUSE:
                io.AddMousePosEvent(e->x * width, e->y * canvas_height);
                break;

            case OVERLAY_EVENT_BUTTON:
                if (e->button >= 0 && e->button < ImGuiMouseButton_COUNT)
                    io.AddMouseButtonEvent(e->button, e->down != 0);
                break;

            case OVERLAY_EVENT_WHEEL:
                io.AddMouseWheelEvent(e->x, e->y);
                break;

            case OVERLAY_EVENT_KEY:
            {
                if (g_capture >= 0)
                {
                    menu_capture_key(e);
                    break;
                }

                /* Only the keys pressed in the OSD: the Alt of the Alt+F12
                 * that opened it would otherwise be let go as if it had been
                 * tapped, which takes ImGui's focus to the title bar */
                int vk = e->vk & 0xFF;

                if (e->down)
                    g_key_down[vk] = true;
                else if (!g_key_down[vk])
                    break;
                else
                    g_key_down[vk] = false;

                io.AddKeyEvent(ImGuiMod_Ctrl, e->ctrl != 0);
                io.AddKeyEvent(ImGuiMod_Shift, e->shift != 0);
                io.AddKeyEvent(ImGuiMod_Alt, e->alt != 0);

                ImGuiKey key = menu_imgui_key(e->vk);

                if (key != ImGuiKey_None)
                    io.AddKeyEvent(key, e->down != 0);

                break;
            }

            case OVERLAY_EVENT_CHAR:
                if (g_capture < 0)
                    io.AddInputCharacterUTF16(e->ch);
                break;
            }
        }
    }
}

static void menu_key_name(int vk, char* buf, size_t size)
{
    const char* name = NULL;
    char text[64];

    switch (vk)
    {
    case 0: name = "None"; break;
    case VK_SHIFT: name = "Shift"; break;
    case VK_CONTROL: name = "Ctrl"; break;
    case VK_MENU: name = "Alt"; break;
    case VK_LSHIFT: name = "Left Shift"; break;
    case VK_RSHIFT: name = "Right Shift"; break;
    case VK_LCONTROL: name = "Left Ctrl"; break;
    case VK_RCONTROL: name = "Right Ctrl"; break;
    case VK_LMENU: name = "Left Alt"; break;
    case VK_RMENU: name = "Right Alt"; break;
    case VK_LWIN: name = "Left Windows"; break;
    case VK_RWIN: name = "Right Windows"; break;
    case VK_APPS: name = "Menu"; break;
    case VK_SNAPSHOT: name = "Print Screen"; break;
    case VK_PAUSE: name = "Pause"; break;
    case VK_PRIOR: name = "Page Up"; break;
    case VK_NEXT: name = "Page Down"; break;
    case VK_LBUTTON: name = "Left mouse button"; break;
    case VK_RBUTTON: name = "Right mouse button"; break;
    case VK_MBUTTON: name = "Middle mouse button"; break;
    }

    if (!name)
    {
        UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        bool extended =
            vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
            vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
            vk == VK_DIVIDE || vk == VK_NUMLOCK;

        LONG lparam = (LONG)((scan << 16) | (extended ? (1 << 24) : 0));

        if (scan && GetKeyNameTextA(lparam, text, sizeof(text)) > 0)
            name = text;
        else
            name = "Key";
    }

    if (vk)
        _snprintf(buf, size - 1, "%s (0x%02X)", name, vk);
    else
        _snprintf(buf, size - 1, "%s", name);

    buf[size - 1] = 0;
}

/* The help pane follows the mouse, or the keyboard's focus */
static void menu_hover(int setting)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
        (ImGui::GetIO().NavVisible && ImGui::IsItemFocused()))
    {
        g_help = setting;
    }
}

static int menu_find_choice(const SETTING* s, const char* value, char* label, size_t label_size)
{
    char v[MENU_TEXT], l[MENU_TEXT];

    for (int i = 0; settings_choice(s, i, v, sizeof(v), l, sizeof(l)); i++)
    {
        if (_stricmp(v, value) == 0)
        {
            if (label)
            {
                strncpy(label, l, label_size - 1);
                label[label_size - 1] = 0;
            }

            return i;
        }
    }

    return -1;
}

/* A combo of the setting's choices, and "other" (when not NULL) after them.
 * Returns the choice picked: -1 = none, -2 = other */
static int menu_choice_combo(const SETTING* s, const char* value, const char* other, bool other_selected)
{
    char preview[MENU_TEXT];
    int current = menu_find_choice(s, value, preview, sizeof(preview));

    if (current < 0 || other_selected)
    {
        strncpy(preview, other ? other : value, sizeof(preview) - 1);
        preview[sizeof(preview) - 1] = 0;
        current = -2;
    }

    int picked = -1;

    if (ImGui::BeginCombo("##choice", preview))
    {
        char v[MENU_TEXT], l[MENU_TEXT];

        for (int i = 0; settings_choice(s, i, v, sizeof(v), l, sizeof(l)); i++)
        {
            ImGui::PushID(i);

            if (ImGui::Selectable(l, current == i))
                picked = i;

            if (current == i)
                ImGui::SetItemDefaultFocus();

            ImGui::PopID();
        }

        if (other)
        {
            if (ImGui::Selectable(other, current == -2))
                picked = -2;

            if (current == -2)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }

    return picked;
}

static void menu_write_choice(int setting, const SETTING* s, int choice)
{
    char v[MENU_TEXT], l[MENU_TEXT];

    if (settings_choice(s, choice, v, sizeof(v), l, sizeof(l)))
        overlay_menu_write(setting, g_scope_game, v);
}

static void menu_widget_bool(int setting, const SETTING* s, const char* value)
{
    char off[32], on[32], label[32];
    settings_choice(s, 0, off, sizeof(off), label, sizeof(label));
    settings_choice(s, 1, on, sizeof(on), label, sizeof(label));

    int parsed = 0;
    bool checked = ds_parse_bool(value, &parsed) && parsed;

    if (ImGui::Checkbox("##bool", &checked))
        overlay_menu_write(setting, g_scope_game, checked ? on : off);

    menu_hover(setting);
}

static void menu_widget_choice(int setting, const SETTING* s, const char* value)
{
    ImGui::SetNextItemWidth(-FLT_MIN);
    int picked = menu_choice_combo(s, value, NULL, false);
    menu_hover(setting);

    if (picked >= 0)
        menu_write_choice(setting, s, picked);
}

static bool menu_is_number(const char* value)
{
    char* end;
    strtol(value, &end, 0);
    return end != value && *end == 0;
}

static void menu_widget_int(int setting, const SETTING* s, const char* value)
{
    bool named = s->choices && menu_find_choice(s, value, NULL, 0) >= 0;

    if (s->choices)
    {
        ImGui::SetNextItemWidth(named ? -FLT_MIN : ImGui::GetContentRegionAvail().x * 0.4f);
        int picked = menu_choice_combo(s, value, "A number", false);
        menu_hover(setting);

        if (picked >= 0)
        {
            menu_write_choice(setting, s, picked);
        }
        else if (picked == -2 && named)
        {
            int n = s->min <= 60 && 60 <= s->max ? 60 : (s->min + s->max) / 2;
            char buf[16];
            _snprintf(buf, sizeof(buf) - 1, "%d", n);
            buf[sizeof(buf) - 1] = 0;
            overlay_menu_write(setting, g_scope_game, buf);
        }

        if (named)
            return;

        ImGui::SameLine();
    }

    int v = g_slider == setting ? g_slider_value : (int)strtol(value, NULL, 0);

    if (!menu_is_number(value) && g_slider != setting)
        v = s->min;

    ImGui::SetNextItemWidth(-FLT_MIN);

    if (ImGui::SliderInt("##int", &v, s->min, s->max, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        g_slider = setting;
        g_slider_value = v;
    }

    menu_hover(setting);

    /* Written when it's let go: a picture setting sets the picture up again */
    if (ImGui::IsItemDeactivated() && g_slider == setting)
    {
        char buf[16];
        _snprintf(buf, sizeof(buf) - 1, "%d", g_slider_value);
        buf[sizeof(buf) - 1] = 0;

        if (strcmp(buf, value))
            overlay_menu_write(setting, g_scope_game, buf);

        g_slider = -1;
    }
}

static void menu_widget_string(int setting, const SETTING* s, const char* value)
{
    bool named = s->choices && menu_find_choice(s, value, NULL, 0) >= 0;
    bool typing = !named || g_custom == setting;

    if (s->choices)
    {
        ImGui::SetNextItemWidth(typing ? ImGui::GetContentRegionAvail().x * 0.4f : -FLT_MIN);
        int picked = menu_choice_combo(s, value, "Other", g_custom == setting);
        menu_hover(setting);

        if (picked >= 0)
        {
            g_custom = -1;
            menu_write_choice(setting, s, picked);
        }
        else if (picked == -2)
        {
            g_custom = setting;
            g_custom_focus = true;
        }

        if (!typing && picked != -2)
            return;

        ImGui::SameLine();
    }

    char buf[MENU_TEXT];
    strncpy(buf, g_text == setting ? g_text_value : value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    if (g_custom == setting && g_custom_focus)
    {
        ImGui::SetKeyboardFocusHere();
        g_custom_focus = false;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##text", buf, sizeof(buf));
    menu_hover(setting);

    if (ImGui::IsItemActive())
    {
        g_text = setting;
        strcpy(g_text_value, buf);
    }

    if (ImGui::IsItemDeactivated() && g_text == setting)
    {
        g_text = -1;

        /* Leading and trailing spaces would be lost in the ini anyway */
        char* start = buf;
        while (*start == ' ')
            start++;

        size_t len = strlen(start);
        while (len > 0 && start[len - 1] == ' ')
            start[--len] = 0;

        if (ImGui::IsItemDeactivatedAfterEdit() && strcmp(start, value))
        {
            /* Nothing = the default, or for the game the value for all games */
            overlay_menu_write(setting, g_scope_game, start[0] ? start : NULL);
            g_custom = -1;
        }
    }
}

static void menu_widget_key(int setting, const SETTING* s, const char* value)
{
    int vk = (int)strtol(value, NULL, 0);
    bool capturing = g_capture == setting;

    char label[96];

    if (capturing)
        strcpy(label, "Press a key (Esc cancels)");
    else
        menu_key_name(vk, label, sizeof(label));

    const char* none = "None";
    float none_width = ImGui::CalcTextSize(none).x + ImGui::GetStyle().FramePadding.x * 2;
    float width = ImGui::GetContentRegionAvail().x - none_width - ImGui::GetStyle().ItemSpacing.x;

    if (capturing)
    {
        g_capture_seen = true;
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }

    if (ImGui::Button(label, ImVec2(width > 0 ? width : 0, 0)))
        menu_capture(capturing ? -1 : setting);

    if (capturing)
        ImGui::PopStyleColor();

    menu_hover(setting);
    ImGui::SameLine();

    if (ImGui::Button("None##clear"))
    {
        if (capturing)
            menu_capture(-1);

        overlay_menu_write(setting, g_scope_game, "0x00");
    }

    menu_hover(setting);
}

static void menu_rows(const char* tab)
{
    float label_width = 0;

    for (int i = 0; i < settings_count(); i++)
    {
        const SETTING* s = settings_get(i);

        if (s->tab && !strcmp(s->tab, tab) && s->label)
        {
            float w = ImGui::CalcTextSize(s->label).x;

            if (w > label_width)
                label_width = w;
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    float mark_width =
        ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2 + style.ItemSpacing.x + ImGui::CalcTextSize("*").x;

    if (!ImGui::BeginTable("##settings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX))
        return;

    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("mark", ImGuiTableColumnFlags_WidthFixed, mark_width);

    for (int i = 0; i < settings_count(); i++)
    {
        const SETTING* s = settings_get(i);

        if (!s->key || !s->tab || strcmp(s->tab, tab))
            continue;

        BOOL set = FALSE;
        char value[MENU_TEXT];
        strncpy(value, overlay_menu_value(i, g_scope_game, &set), sizeof(value) - 1);
        value[sizeof(value) - 1] = 0;

        ImGui::TableNextRow();
        ImGui::PushID(i);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(s->label ? s->label : s->key);
        menu_hover(i);

        ImGui::TableNextColumn();

        switch (s->kind)
        {
        case SETTING_BOOL: menu_widget_bool(i, s, value); break;
        case SETTING_CHOICE: menu_widget_choice(i, s, value); break;
        case SETTING_INT: menu_widget_int(i, s, value); break;
        case SETTING_STRING: menu_widget_string(i, s, value); break;
        case SETTING_KEY: menu_widget_key(i, s, value); break;
        default: break;
        }

        ImGui::TableNextColumn();

        if (g_scope_game)
        {
            if (set)
            {
                if (ImGui::SmallButton("Reset"))
                    overlay_menu_write(i, TRUE, NULL);

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("This game has its own value. Reset: use the value for all games");

                ImGui::SameLine();
            }
        }
        else
        {
            BOOL game_set = FALSE;
            const char* game_value = overlay_menu_value(i, TRUE, &game_set);

            if (game_set)
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("game");

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s has its own value: %s", overlay_menu_game(), game_value);

                ImGui::SameLine();
            }
        }

        if (s->apply == SETTING_RESTART)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("*");

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Takes effect when the game starts again");
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

static void menu_help(void)
{
    const SETTING* s = g_help >= 0 ? settings_get(g_help) : NULL;

    if (!s || !s->key)
    {
        char close[96];
        int i = settings_index("keysettingsosd");
        menu_key_name(i >= 0 ? (int)strtol(overlay_menu_value(i, TRUE, NULL), NULL, 0) : 0, close, sizeof(close));

        ImGui::TextWrapped(
            "Point at a setting to see what it does. The settings are saved in directcrt.ini, for all games or "
            "for this game alone (in its own section, over the ones for all games).");
        ImGui::TextWrapped("* = takes effect when the game starts again; the rest take effect at once.");
        ImGui::TextWrapped("Esc or Alt + %s closes the settings. The game gets no keyboard or mouse input while they're open.", close);
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "%s", s->label ? s->label : s->key);

    /* The help's lines; one starting with two spaces goes on with the one before */
    if (s->help)
    {
        char line[2048];
        size_t len = 0;
        const char* p = s->help;

        while (*p)
        {
            const char* end = strchr(p, '\n');
            size_t n = end ? (size_t)(end - p) : strlen(p);
            bool more = n >= 2 && p[0] == ' ' && p[1] == ' ';

            if (!more && len > 0)
            {
                line[len] = 0;
                ImGui::TextWrapped("%s", line);
                len = 0;
            }

            const char* from = more ? p + 1 : p;
            size_t count = more ? n - 1 : n;

            if (len == 0 && more)
            {
                from++;
                count--;
            }

            if (len + count >= sizeof(line))
                count = sizeof(line) - 1 - len;

            memcpy(line + len, from, count);
            len += count;

            p = end ? end + 1 : p + n;
        }

        if (len > 0)
        {
            line[len] = 0;
            ImGui::TextWrapped("%s", line);
        }
    }

    ImGui::Spacing();

    const char* apply =
        s->apply == SETTING_LIVE ? "Takes effect at once" :
        s->apply == SETTING_PICTURE ? "Takes effect at once; the picture is set up again" :
        "Takes effect when the game starts again";

    ImGui::TextDisabled("%s=%s (default: %s). %s", s->key, overlay_menu_value(g_help, g_scope_game, NULL), s->value, apply);
}

static void menu_draw(int width, int canvas_height, float font_size, float scale)
{
    float margin = 16.0f * scale;
    float w = 64.0f * font_size;
    float h = 38.0f * font_size;

    if (w > width - 2 * margin)
        w = width - 2 * margin;

    if (h > canvas_height - 2 * margin)
        h = canvas_height - 2 * margin;

    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, canvas_height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);

    if (!g_menu_drawn)
        ImGui::SetNextWindowFocus();

    bool open = true;
    g_capture_seen = false;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("DirectCRT settings###settings", &open, flags))
    {
        /* Esc closes it, unless it's for a text field or a list */
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && g_capture < 0 && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            open = false;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Settings for");
        ImGui::SameLine();

        if (ImGui::RadioButton("all games", !g_scope_game) && g_scope_game)
        {
            g_scope_game = false;
            menu_reset();
        }

        ImGui::SameLine();

        char game[MAX_PATH + 32];
        _snprintf(game, sizeof(game) - 1, "this game (%s)", overlay_menu_game());
        game[sizeof(game) - 1] = 0;

        if (ImGui::RadioButton(game, g_scope_game) && !g_scope_game)
        {
            g_scope_game = true;
            menu_reset();
        }

        float help_height =
            ImGui::GetTextLineHeightWithSpacing() * 7 + ImGui::GetStyle().WindowPadding.y * 2;

        if (ImGui::BeginTabBar("##tabs"))
        {
            for (int t = 0; settings_tab(t); t++)
            {
                if (ImGui::BeginTabItem(settings_tab(t)))
                {
                    /* The help is about this tab's settings */
                    static int shown_tab = -1;

                    if (t != shown_tab)
                    {
                        shown_tab = t;
                        g_help = -1;
                    }

                    if (ImGui::BeginChild("##rows", ImVec2(0, -(help_height + ImGui::GetStyle().ItemSpacing.y))))
                        menu_rows(settings_tab(t));

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }

        if (ImGui::BeginChild("##help", ImVec2(0, 0), ImGuiChildFlags_Borders))
            menu_help();

        ImGui::EndChild();
    }

    ImGui::End();

    /* The hotkey being set went off screen (another tab) */
    if (g_capture >= 0 && !g_capture_seen)
        menu_capture(-1);

    if (!open)
        overlay_menu_request_close();
}

/* ------------------------------------------------------------------- frame */

/* One ImGui frame with the OSDs in it; the renderer backend's NewFrame comes
 * first and its RenderDrawData after */
static void overlay_build_frame(int width, int height, int canvas_height)
{
    bool menu = overlay_menu_open() != 0;
    bool status_shown = overlay_status_visible() != 0;

    OVERLAY_STATUS status;

    if (status_shown)
        overlay_get_status(&status, width, canvas_height);

    float font_size = status_shown ? status.font_size : 13.0f;
    float scale = status_shown ? status.scale : 1.0f;

    /* Both in the settings OSD's size while it's open */
    if (menu)
    {
        font_size = overlay_menu_font_size(canvas_height);
        scale = font_size / 13.0f;
        status.font_size = font_size;
        status.scale = scale;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);

    float dt = g_last_frame.QuadPart ? (float)((double)(now.QuadPart - g_last_frame.QuadPart) / freq.QuadPart) : 0.0f;
    io.DeltaTime = dt > 0.0001f ? dt : 1.0f / 60.0f;
    g_last_frame = now;

    if (scale != g_style_scale)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style = g_base_style;
        style.ScaleAllSizes(scale);
        g_style_scale = scale;
    }

    if (menu)
    {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        menu_input(width, canvas_height);

        /* ImGui draws the cursor when the game keeps Windows' hidden */
        CURSORINFO ci = { sizeof(ci) };
        io.MouseDrawCursor = GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING);
    }
    else
    {
        if (g_menu_drawn)
        {
            io.ClearInputKeys();
            io.ClearInputMouse();
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            memset(g_key_down, 0, sizeof(g_key_down));
            menu_reset();
        }

        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        io.MouseDrawCursor = false;
    }

    ImGui::NewFrame();
    ImGui::PushFont(g_font, font_size);

    if (status_shown)
        overlay_draw_status(status, width, canvas_height);

    if (menu)
        menu_draw(width, canvas_height, font_size, scale);

    g_menu_drawn = menu;

    ImGui::PopFont();
    ImGui::Render();
}

/* ------------------------------------------------------------ Direct3D 11 */

BOOL overlay_d3d11_draw(
    ID3D11Device* device,
    ID3D11DeviceContext* ctx,
    ID3D11RenderTargetView* rtv,
    int width,
    int height)
{
    /* One more frame after the settings OSD closes, to let go of its input */
    if ((!overlay_visible() && !g_menu_drawn) || !device || !ctx || !rtv || width <= 0 || height <= 0)
        return TRUE;

    if (g_backend == OVERLAY_DX11 && g_dx_device != device)
        overlay_d3d11_release();

    if (g_backend != OVERLAY_DX11)
    {
        if (g_dx_failed || g_backend != OVERLAY_NONE)
            return FALSE;

        /* The backend compiles its shaders with D3DCompile, from the delay
         * loaded d3dcompiler_47.dll (a part of Windows since 10): loaded here
         * first, from System32, so a missing one fails here and not with the
         * delay load's exception */
        if (!LoadLibraryExA("d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32) &&
            !LoadLibraryA("d3dcompiler_47.dll"))
        {
            g_dx_failed = true;
            return FALSE;
        }

        overlay_init_context();

        if (!ImGui_ImplDX11_Init(device, ctx))
        {
            g_dx_failed = true;
            return FALSE;
        }

        g_backend = OVERLAY_DX11;
        g_dx_device = device;
    }

    ImGui_ImplDX11_NewFrame();
    overlay_build_frame(width, height, height);

    ctx->OMSetRenderTargets(1, &rtv, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return TRUE;
}

void overlay_d3d11_release(void)
{
    if (g_backend != OVERLAY_DX11)
        return;

    ImGui_ImplDX11_Shutdown();
    g_backend = OVERLAY_NONE;
    g_dx_device = NULL;
}

/* ----------------------------------------------------------------- OpenGL */

BOOL overlay_ogl_draw(int width, int height, int canvas_height, const char* glsl_version)
{
    if ((!overlay_visible() && !g_menu_drawn) || width <= 0 || height <= 0 || canvas_height <= 0)
        return TRUE;

    if (g_backend != OVERLAY_OGL)
    {
        if (g_ogl_failed || g_backend != OVERLAY_NONE)
            return FALSE;

        overlay_init_context();

        if (!ImGui_ImplOpenGL3_Init(glsl_version))
        {
            g_ogl_failed = true;
            return FALSE;
        }

        g_backend = OVERLAY_OGL;
    }

    /* The canvas is the framebuffer's top canvas_height lines: the window is
     * a line taller than the screen in nonexclusive fullscreen */
    ImGui_ImplOpenGL3_NewFrame();
    overlay_build_frame(width, height, canvas_height);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return TRUE;
}

void overlay_ogl_release(void)
{
    if (g_backend != OVERLAY_OGL)
        return;

    ImGui_ImplOpenGL3_Shutdown();
    g_backend = OVERLAY_NONE;
}
