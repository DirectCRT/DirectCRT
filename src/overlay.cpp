/* The status OSD and the settings OSD, drawn with Dear ImGui (third_party/imgui)
 * over the finished frame. There's no platform backend: the display size and
 * the time step are set here, and the settings OSD's input comes from
 * overlay_menu.c's queue. The ImGui context lives as long as the process; the
 * renderer backend is set up the first time the OSD is drawn and shut down
 * with the renderer's resources. */

#include <windows.h>
#include <d3d11.h>
#include <math.h>
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
extern "C" HMODULE g_ddraw_module;

static ImFont* g_font;
#define OVERLAY_FONT_SCALE 1.2f
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

    /* Source Sans 3 (third_party/fonts), built in; ImGui's own if it isn't there */
    HRSRC res = FindResourceA(g_ddraw_module, "OVERLAY_FONT", MAKEINTRESOURCEA(10) /* RT_RCDATA */);
    HGLOBAL mem = res ? LoadResource(g_ddraw_module, res) : NULL;
    void* data = mem ? LockResource(mem) : NULL;
    DWORD size = res ? SizeofResource(g_ddraw_module, res) : 0;

    g_font = NULL;

    if (data && size)
    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false; /* the DLL's own memory */

        /* Its size counts the accents above and the tails below; scaled up,
         * its letters are about as tall as ImGui's own font's */
        cfg.ExtraSizeScale = OVERLAY_FONT_SCALE;
        g_font = io.Fonts->AddFontFromMemoryTTF(data, (int)size, 13.0f, &cfg);
    }

    if (!g_font)
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

    ImGui::SetNextWindowBgAlpha(status.opacity);

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

            /* the graph's ground fades with the window's */
            ImVec4 ground = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
            ground.w *= status.opacity;
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ground);

            ImGui::PlotLines(
                "##frames",
                status.frame_ms,
                status.frame_count,
                0,
                scale,
                0.0f,
                status.graph_max_ms,
                ImVec2(graph_width, 48.0f * status.scale));

            ImGui::PopStyleColor();
        }
    }

    ImGui::End();
}

/* -------------------------------------------------------- the settings OSD */

#define MENU_TEXT 264

static bool g_menu_drawn;      /* the settings OSD was in the last frame */
static int g_capture = -1;     /* the hotkey being set */
static bool g_capture_seen;    /* its button was drawn this frame */
static int g_slider = -1;      /* the slider being dragged, and its value */
static int g_slider_value;
static int g_number = -1;      /* the number box being typed in, and its number */
static int g_number_value;
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
    g_number = -1;
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

    overlay_menu_write(g_capture, value);
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

#define MENU_WHOLE -1 /* the tooltip is about the setting, not one of its choices */
#define MENU_OTHER -2 /* a value of its own: "A number" or "Other" */

/* The setting's help in a tooltip, when the item before it is under the mouse
 * or has the keyboard's focus; for a choice, only that choice's lines of it */
static void menu_tooltip(int setting, int choice)
{
    if (!ImGui::BeginItemTooltip())
        return;

    const SETTING* s = settings_get(setting);
    char text[2048];
    bool own = false;

    if (choice != MENU_WHOLE)
    {
        char v[MENU_TEXT], l[MENU_TEXT];

        if (choice == MENU_OTHER)
            own = settings_choice_help(s, NULL, text, sizeof(text)) != 0;
        else if (settings_choice(s, choice, v, sizeof(v), l, sizeof(l)))
            own = settings_choice_help(s, v, text, sizeof(text)) != 0;
    }

    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "%s", s->label ? s->label : s->key);

    if (own)
    {
        ImGui::TextUnformatted(text);
    }
    else
    {
        /* A setting sharing a row (the green and blue gains) has the help of the row's first */
        const SETTING* help = s;

        for (int i = setting; !help->help && (help->flags & SETTING_JOINED) && i > 0;)
            help = settings_get(--i);

        const char* cursor = help->help ? help->help : "";

        while (settings_help_line(&cursor, text, sizeof(text)))
            ImGui::TextUnformatted(text);
    }

    ImGui::Spacing();

    const char* apply =
        s->apply == SETTING_LIVE ? "Takes effect at once" :
        s->apply == SETTING_PICTURE ? "Takes effect at once; the picture is set up again" :
        "Takes effect when the game starts again";

    ImGui::TextDisabled("%s=%s (default: %s). %s", s->key, overlay_menu_value(setting, NULL), s->value, apply);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/* Radio buttons for the setting's choices, as many to a line as fit, and
 * 'other' after them when it isn't NULL. current = the choice that's on, or
 * MENU_OTHER. Returns the one clicked: -1 = none, MENU_OTHER = other */
static int menu_radios(int setting, const SETTING* s, int current, const char* other)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    int picked = -1;
    char v[MENU_TEXT], l[MENU_TEXT];

    for (int i = 0;; i++)
    {
        bool is_other = !settings_choice(s, i, v, sizeof(v), l, sizeof(l));

        if (is_other && !other)
            break;

        const char* label = is_other ? other : l;

        if (i > 0)
        {
            float w = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label, NULL, true).x;

            if (ImGui::GetItemRectMax().x + style.ItemSpacing.x * 2 + w <= right)
                ImGui::SameLine(0.0f, style.ItemSpacing.x * 2);
        }

        ImGui::PushID(i);

        if (ImGui::RadioButton(label, current == (is_other ? MENU_OTHER : i)))
            picked = is_other ? MENU_OTHER : i;

        menu_tooltip(setting, is_other ? MENU_OTHER : i);
        ImGui::PopID();

        if (is_other)
            break;
    }

    return picked;
}

/* Writes choice i, if it isn't the value already */
static void menu_write_choice(int setting, const SETTING* s, int choice, const char* value)
{
    char v[MENU_TEXT], l[MENU_TEXT];

    if (settings_choice(s, choice, v, sizeof(v), l, sizeof(l)) && strcmp(v, value))
        overlay_menu_write(setting, v);
}

static void menu_widget_bool(int setting, const SETTING* s, const char* value)
{
    char off[32], on[32], label[32];
    settings_choice(s, 0, off, sizeof(off), label, sizeof(label));
    settings_choice(s, 1, on, sizeof(on), label, sizeof(label));

    int parsed = 0;
    bool checked = ds_parse_bool(value, &parsed) && parsed;

    if (ImGui::Checkbox("##bool", &checked))
        overlay_menu_write(setting, checked ? on : off);

    menu_tooltip(setting, MENU_WHOLE);
}

static void menu_widget_choice(int setting, const SETTING* s, const char* value)
{
    /* A value the OSD doesn't name (DOSBox's "auto" for the colour profile) is the default */
    int current = menu_find_choice(s, value, NULL, 0);

    if (current < 0)
        current = menu_find_choice(s, s->value, NULL, 0);

    int picked = menu_radios(setting, s, current, NULL);

    if (picked >= 0)
        menu_write_choice(setting, s, picked, value);
}

static bool menu_is_number(const char* value)
{
    char* end;
    strtol(value, &end, 0);
    return end != value && *end == 0;
}

static int menu_clamp(const SETTING* s, int v)
{
    return v < s->min ? s->min : v > s->max ? s->max : v;
}

/* v rounded down to a multiple of step */
static int menu_floor(int v, int step)
{
    return v >= 0 ? v / step * step : -((-v + step - 1) / step * step);
}

/* A slider's value moved to its step: the nearest while it's dragged, the
 * next one on in the way it moved with the keyboard */
static int menu_snap(int v, int old, int step, bool dragged)
{
    if (step <= 1)
        return v;

    if (dragged)
        return menu_floor(v + step / 2, step);

    return v > old ? -menu_floor(-v, step) : menu_floor(v, step);
}

static void menu_write_int(int setting, const char* value, int v)
{
    char buf[16];
    _snprintf(buf, sizeof(buf) - 1, "%d", v);
    buf[sizeof(buf) - 1] = 0;

    if (strcmp(buf, value))
        overlay_menu_write(setting, buf);
}

/* The colour of a slider for one colour (the RGB gains), or NULL */
static const ImVec4* menu_tint(const SETTING* s)
{
    static const struct
    {
        const char* key;
        ImVec4 color;
    } tints[] = {
        { "red_gain", ImVec4(0.86f, 0.30f, 0.30f, 1.0f) },
        { "green_gain", ImVec4(0.35f, 0.75f, 0.38f, 1.0f) },
        { "blue_gain", ImVec4(0.40f, 0.56f, 0.96f, 1.0f) },
    };

    for (size_t i = 0; i < sizeof(tints) / sizeof(tints[0]); i++)
    {
        if (!strcmp(s->key, tints[i].key))
            return &tints[i].color;
    }

    return NULL;
}

/* A slider and a box to type the number in, 'width' wide together. The slider
 * moves in the setting's steps; a number typed in needn't be one */
static void menu_widget_int(int setting, const SETTING* s, const char* value, float width)
{
    if (s->choices)
    {
        bool named = menu_find_choice(s, value, NULL, 0) >= 0 && g_custom != setting;
        int picked = menu_radios(setting, s, named ? menu_find_choice(s, value, NULL, 0) : MENU_OTHER, "A number");

        if (picked >= 0)
        {
            g_custom = -1;
            menu_write_choice(setting, s, picked, value);
            return;
        }

        /* The slider shows; the value is written when it's moved */
        if (picked == MENU_OTHER)
            g_custom = setting;
        else if (named)
            return;
    }

    int v;

    if (g_slider == setting)
        v = g_slider_value;
    else if (menu_is_number(value))
        v = (int)strtol(value, NULL, 0);
    else if (menu_is_number(s->value))
        v = (int)strtol(s->value, NULL, 0); /* "auto" (as in DOSBox) is the default */
    else
        v = s->min <= 60 && 60 <= s->max ? 60 : (s->min + s->max) / 2;

    v = menu_clamp(s, v);

    const ImGuiStyle& style = ImGui::GetStyle();
    char lo[16], hi[16];
    _snprintf(lo, sizeof(lo) - 1, "%d", s->min);
    _snprintf(hi, sizeof(hi) - 1, "%d", s->max);
    lo[sizeof(lo) - 1] = hi[sizeof(hi) - 1] = 0;

    /* The box fits the longest number in the range, and the cursor */
    float lo_width = ImGui::CalcTextSize(lo).x;
    float hi_width = ImGui::CalcTextSize(hi).x;
    float box = (lo_width > hi_width ? lo_width : hi_width) + ImGui::CalcTextSize("0").x + style.FramePadding.x * 2;

    if (width > ImGui::GetContentRegionAvail().x)
        width = ImGui::GetContentRegionAvail().x;

    float slider = width - box - style.ItemInnerSpacing.x;

    if (slider < ImGui::GetFrameHeight())
        slider = ImGui::GetFrameHeight();

    const ImVec4* tint = menu_tint(s);

    if (tint)
    {
        ImVec4 c = *tint;
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, c);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(c.x * 0.5f + 0.5f, c.y * 0.5f + 0.5f, c.z * 0.5f + 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.x, c.y, c.z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(c.x, c.y, c.z, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(c.x, c.y, c.z, 0.42f));
    }

    int old = v;
    ImGui::SetNextItemWidth(slider);

    if (ImGui::SliderInt("##int", &v, s->min, s->max, "", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
    {
        g_slider = setting;
        g_slider_value = menu_clamp(s, menu_snap(v, old, s->step, ImGui::IsMouseDown(ImGuiMouseButton_Left)));
        v = g_slider_value;
    }

    if (tint)
        ImGui::PopStyleColor(5);

    menu_tooltip(setting, s->choices ? MENU_OTHER : MENU_WHOLE);

    /* Written when it's let go: a picture setting sets the picture up again */
    if (ImGui::IsItemDeactivated() && g_slider == setting)
    {
        menu_write_int(setting, value, g_slider_value);
        g_slider = -1;
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(box);

    /* While it's typed in, ImGui keeps the text; the number is written when it's left */
    int n = v;

    if (ImGui::InputInt("##number", &n, 0, 0))
    {
        g_number = setting;
        g_number_value = n;
    }

    menu_tooltip(setting, s->choices ? MENU_OTHER : MENU_WHOLE);

    if (ImGui::IsItemDeactivated() && g_number == setting)
    {
        if (ImGui::IsItemDeactivatedAfterEdit())
            menu_write_int(setting, value, menu_clamp(s, g_number_value));

        g_number = -1;
    }
}

static void menu_widget_string(int setting, const SETTING* s, const char* value)
{
    if (s->choices)
    {
        bool named = menu_find_choice(s, value, NULL, 0) >= 0 && g_custom != setting;
        int picked = menu_radios(setting, s, named ? menu_find_choice(s, value, NULL, 0) : MENU_OTHER, "Other");

        if (picked >= 0)
        {
            g_custom = -1;
            menu_write_choice(setting, s, picked, value);
            return;
        }

        if (picked == MENU_OTHER && named)
        {
            g_custom = setting;
            g_custom_focus = true;
        }
        else if (named)
        {
            return;
        }
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
    menu_tooltip(setting, s->choices ? MENU_OTHER : MENU_WHOLE);

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
            /* Nothing = the default */
            overlay_menu_write(setting, start[0] ? start : NULL);
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

    menu_tooltip(setting, MENU_WHOLE);
    ImGui::SameLine();

    if (ImGui::Button("None##clear"))
    {
        if (capturing)
            menu_capture(-1);

        overlay_menu_write(setting, "0x00");
    }

    menu_tooltip(setting, MENU_WHOLE);
}

/* Whether two values of a setting are the same: as numbers, as on/off, or as text */
static bool menu_same(const SETTING* s, const char* a, const char* b)
{
    int x = 0, y = 0;

    if (s->kind == SETTING_BOOL && ds_parse_bool(a, &x) && ds_parse_bool(b, &y))
        return !x == !y;

    if (menu_is_number(a) && menu_is_number(b))
        return strtol(a, NULL, 0) == strtol(b, NULL, 0);

    return _stricmp(a, b) == 0;
}

/* The setting has a value other than its default */
static bool menu_changed(int setting)
{
    const SETTING* s = settings_get(setting);
    return !menu_same(s, overlay_menu_value(setting, NULL), s->value);
}

/* Adds a value to a list of them, as the OSD shows it */
static void menu_append_value(char* buf, size_t size, const SETTING* s, const char* value)
{
    char text[MENU_TEXT];

    if (s->kind == SETTING_KEY)
    {
        menu_key_name((int)strtol(value, NULL, 0), text, sizeof(text));
        value = text;
    }
    else if (s->choices && menu_find_choice(s, value, text, sizeof(text)) >= 0)
    {
        value = text;
    }

    size_t len = strlen(buf);

    if (len + 1 < size)
    {
        _snprintf(buf + len, size - len - 1, "%s%s", len ? ", " : "", value);
        buf[size - 1] = 0;
    }
}

/* A button with a circular arrow on it, a frame high; dim when it's disabled */
static bool menu_reset_icon(bool enabled)
{
    float h = ImGui::GetFrameHeight();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool clicked = ImGui::Button("##reset", ImVec2(h, h));
    ImGui::PopStyleColor();

    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    float r = h * 0.26f;
    float thickness = h * 0.085f > 1.0f ? h * 0.085f : 1.0f;
    ImU32 color = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    /* Clockwise on the screen from the upper right to the upper left, with
     * the head at the start, pointing back: an arrow going anticlockwise */
    const float pi = 3.14159265f;
    float start = -0.30f * pi;
    draw->PathArcTo(c, r, start, 1.25f * pi, 24);
    draw->PathStroke(color, thickness);

    float cs = cosf(start), sn = sinf(start);
    ImVec2 p(c.x + r * cs, c.y + r * sn);
    ImVec2 d(sn, -cs); /* along the arc, backwards */
    float k = h * 0.16f;

    draw->AddTriangleFilled(
        ImVec2(p.x + d.x * k, p.y + d.y * k),
        ImVec2(p.x - d.x * k * 0.3f + cs * k, p.y - d.y * k * 0.3f + sn * k),
        ImVec2(p.x - d.x * k * 0.3f - cs * k, p.y - d.y * k * 0.3f - sn * k),
        color);

    return clicked;
}

/* The reset button of a row: settings first to first + count - 1 */
static void menu_reset_button(int first, int count)
{
    bool changed = false;
    char values[MENU_TEXT * 2] = "";

    for (int m = first; m < first + count; m++)
    {
        changed = changed || menu_changed(m);
        menu_append_value(values, sizeof(values), settings_get(m), settings_get(m)->value);
    }

    ImGui::BeginDisabled(!changed);

    if (menu_reset_icon(changed))
    {
        for (int m = first; m < first + count; m++)
        {
            if (g_custom == m)
                g_custom = -1;

            if (g_capture == m)
                menu_capture(-1);

            if (menu_changed(m))
                overlay_menu_write(m, settings_get(m)->value);
        }
    }

    ImGui::EndDisabled();
    ImGui::SetItemTooltip(changed ? "Reset to the default (%s)" : "This is the default (%s)", values);
}

static bool menu_begin_table(int n, float label_width, float mark_width)
{
    char id[32];
    _snprintf(id, sizeof(id) - 1, "##settings%d", n);
    id[sizeof(id) - 1] = 0;

    if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX))
        return false;

    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableSetupColumn("mark", ImGuiTableColumnFlags_WidthFixed, mark_width);
    return true;
}

/* A tab's rows: a table of them between its headings. A setting marked
 * SETTING_JOINED shares the row of the one before */
static void menu_rows(const char* tab)
{
    float label_width = 0;

    for (int i = 0; i < settings_count(); i++)
    {
        const SETTING* s = settings_get(i);

        if (s->key && s->tab && !strcmp(s->tab, tab) && s->label && !(s->flags & SETTING_JOINED))
        {
            float w = ImGui::CalcTextSize(s->label).x;

            if (w > label_width)
                label_width = w;
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    float mark_width = ImGui::CalcTextSize("*").x;

    int tables = 0;
    bool open = false;
    bool gated = false; /* a SETTING_GATE before is off */

    for (int i = 0; i < settings_count(); i++)
    {
        const SETTING* s = settings_get(i);

        if (!s->tab || strcmp(s->tab, tab))
            continue;

        if (s->kind == SETTING_HEADING)
        {
            if (open)
            {
                ImGui::EndTable();
                open = false;
            }

            ImGui::Spacing();
            ImGui::SeparatorText(s->label);

            if (s->help)
                ImGui::SetItemTooltip("%s", s->help);

            continue;
        }

        if (!s->key || (s->flags & SETTING_JOINED))
            continue;

        if (!open && !(open = menu_begin_table(tables++, label_width, mark_width)))
            return;

        int count = 1;

        while (i + count < settings_count() && (settings_get(i + count)->flags & SETTING_JOINED))
            count++;

        ImGui::TableNextRow();
        ImGui::PushID(i);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(s->label ? s->label : s->key);
        menu_tooltip(i, MENU_WHOLE);

        ImGui::TableNextColumn();

        float width = ImGui::GetContentRegionAvail().x;
        width = (width - style.ItemSpacing.x * (count - 1)) / count;

        ImGui::BeginDisabled(gated);

        for (int m = i; m < i + count; m++)
        {
            const SETTING* ms = settings_get(m);
            char value[MENU_TEXT];
            strncpy(value, overlay_menu_value(m, NULL), sizeof(value) - 1);
            value[sizeof(value) - 1] = 0;

            if (m > i)
                ImGui::SameLine(0.0f, style.ItemSpacing.x);

            ImGui::PushID(m);

            switch (ms->kind)
            {
            case SETTING_BOOL: menu_widget_bool(m, ms, value); break;
            case SETTING_CHOICE: menu_widget_choice(m, ms, value); break;
            case SETTING_INT: menu_widget_int(m, ms, value, width); break;
            case SETTING_STRING: menu_widget_string(m, ms, value); break;
            case SETTING_KEY: menu_widget_key(m, ms, value); break;
            default: break;
            }

            ImGui::PopID();

            if (ms->flags & SETTING_GATE)
            {
                int on = 0;
                gated = !(ds_parse_bool(value, &on) && on);
            }
        }

        ImGui::EndDisabled();

        ImGui::TableNextColumn();
        menu_reset_button(i, count);

        ImGui::TableNextColumn();

        if (s->apply == SETTING_RESTART)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("*");
            ImGui::SetItemTooltip("Takes effect when the game starts again");
        }

        ImGui::PopID();
        i += count - 1;
    }

    if (open)
        ImGui::EndTable();
}

static void menu_draw(int width, int canvas_height, float font_size, float scale)
{
    float margin = 16.0f * scale;
    float w = 50.0f * font_size;
    float h = 36.0f * font_size;

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

        char close[96];
        int key = settings_index("keysettingsosd");
        menu_key_name(key >= 0 ? (int)strtol(overlay_menu_value(key, NULL), NULL, 0) : 0, close, sizeof(close));

        char footer[256];
        _snprintf(
            footer, sizeof(footer) - 1,
            "Point at a setting or a choice for help. * = takes effect when the game starts again. "
            "Esc or Alt + %s closes this; the game gets no input while it's open.", close);
        footer[sizeof(footer) - 1] = 0;

        float footer_height =
            ImGui::CalcTextSize(footer, NULL, false, ImGui::GetContentRegionAvail().x).y + ImGui::GetStyle().ItemSpacing.y;

        if (ImGui::BeginTabBar("##tabs"))
        {
            for (int t = 0; settings_tab(t); t++)
            {
                if (ImGui::BeginTabItem(settings_tab(t)))
                {
                    if (ImGui::BeginChild("##rows", ImVec2(0, -footer_height)))
                        menu_rows(settings_tab(t));

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", footer);
        ImGui::PopStyleColor();
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
/* The style at a scale (the font's size / 13); the two OSDs can have different ones */
static void overlay_use_style(float scale)
{
    static ImGuiStyle styles[2];
    static float scales[2];
    static int next;

    int i = scales[0] == scale ? 0 : scales[1] == scale ? 1 : -1;

    if (i < 0)
    {
        i = next;
        next ^= 1;
        styles[i] = g_base_style;
        styles[i].ScaleAllSizes(scale);
        scales[i] = scale;
    }

    if (g_style_scale != scale)
    {
        ImGui::GetStyle() = styles[i];
        g_style_scale = scale;
    }
}

static void overlay_build_frame(int width, int height, int canvas_height)
{
    bool menu = overlay_menu_open() != 0;
    bool status_shown = overlay_status_visible() != 0;

    OVERLAY_STATUS status;

    if (status_shown)
        overlay_get_status(&status, width, canvas_height);

    /* Each in its own size */
    float menu_font = menu ? overlay_menu_font_size(canvas_height) : 13.0f;
    float menu_scale = menu_font / 13.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);

    float dt = g_last_frame.QuadPart ? (float)((double)(now.QuadPart - g_last_frame.QuadPart) / freq.QuadPart) : 0.0f;
    io.DeltaTime = dt > 0.0001f ? dt : 1.0f / 60.0f;
    g_last_frame = now;

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

    /* The menu's style for the frame (and the mouse cursor ImGui draws) */
    overlay_use_style(menu ? menu_scale : status_shown ? status.scale : 1.0f);
    ImGui::NewFrame();

    if (status_shown)
    {
        overlay_use_style(status.scale);
        ImGui::PushFont(g_font, status.font_size);
        overlay_draw_status(status, width, canvas_height);
        ImGui::PopFont();
    }

    if (menu)
    {
        overlay_use_style(menu_scale);
        ImGui::PushFont(g_font, menu_font);
        menu_draw(width, canvas_height, menu_font, menu_scale);
        ImGui::PopFont();
    }

    g_menu_drawn = menu;

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
