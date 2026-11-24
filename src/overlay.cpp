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
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_opengl3.h"
#include "overlay.h"
#include "overlay_icons.h"

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

static ImFont* g_font;   /* Source Sans 3 */
static ImFont* g_icons;  /* Material Icons, cut down (inc/overlay_icons.h); NULL = no icons */
#define OVERLAY_FONT_SCALE 1.2f
static ImGuiStyle g_base_style;
static float g_style_scale;
static LARGE_INTEGER g_last_frame;

static OVERLAY_BACKEND g_backend;
static ID3D11Device* g_dx_device;  /* the device the backend was set up with */
static bool g_dx_failed;
static bool g_ogl_failed;

/* ---------------------------------------------------------------- the look */

/* Dark, with one blue for what's on and what has the focus */
static const ImVec4 COL_PANEL(0.086f, 0.090f, 0.102f, 0.97f);
static const ImVec4 COL_BAR(0.059f, 0.063f, 0.071f, 1.0f);
static const ImVec4 COL_DIALOG(0.114f, 0.119f, 0.135f, 0.99f);
static const ImVec4 COL_ROW_HOVER(0.165f, 0.173f, 0.192f, 1.0f);
static const ImVec4 COL_ROW_ACTIVE(0.200f, 0.210f, 0.235f, 1.0f);
static const ImVec4 COL_PILL(0.137f, 0.145f, 0.165f, 1.0f);
static const ImVec4 COL_PILL_HOVER(0.190f, 0.200f, 0.225f, 1.0f);
static const ImVec4 COL_TEXT(0.925f, 0.925f, 0.930f, 1.0f);
static const ImVec4 COL_DIM(0.600f, 0.610f, 0.640f, 1.0f);
static const ImVec4 COL_HEADING(0.650f, 0.660f, 0.690f, 1.0f);
static const ImVec4 COL_RULE(0.240f, 0.250f, 0.270f, 1.0f);
static const ImVec4 COL_ACCENT(0.300f, 0.560f, 0.900f, 1.0f);
static const ImVec4 COL_ACCENT_HI(0.480f, 0.690f, 0.960f, 1.0f);
static const ImVec4 COL_TRACK(0.290f, 0.300f, 0.320f, 1.0f);
static const ImVec4 COL_KNOB(0.880f, 0.880f, 0.890f, 1.0f);

/* A colour of the palette; GetColorU32 fades it with the style's alpha (greyed out items) */
static ImU32 ui_col(const ImVec4& c, float alpha = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

static void overlay_set_style(ImGuiStyle& style)
{
    ImGui::StyleColorsDark(&style);

    /* At 13 pixels; overlay_use_style scales it to the font */
    style.WindowPadding = ImVec2(9.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 2.0f);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.HoverDelayNormal = 0.6f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = COL_TEXT;
    c[ImGuiCol_TextDisabled] = COL_DIM;
    c[ImGuiCol_WindowBg] = COL_PANEL;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = COL_DIALOG;
    c[ImGuiCol_Border] = COL_RULE;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = COL_PILL;
    c[ImGuiCol_FrameBgHovered] = COL_PILL_HOVER;
    c[ImGuiCol_FrameBgActive] = COL_ROW_ACTIVE;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.31f, 0.34f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.39f, 0.42f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.45f, 0.46f, 0.50f, 1.0f);
    c[ImGuiCol_CheckMark] = COL_ACCENT;
    c[ImGuiCol_SliderGrab] = COL_ACCENT;
    c[ImGuiCol_SliderGrabActive] = COL_ACCENT_HI;
    c[ImGuiCol_Button] = COL_PILL;
    c[ImGuiCol_ButtonHovered] = COL_PILL_HOVER;
    c[ImGuiCol_ButtonActive] = COL_ROW_ACTIVE;
    c[ImGuiCol_Header] = COL_ROW_HOVER;
    c[ImGuiCol_HeaderHovered] = COL_ROW_HOVER;
    c[ImGuiCol_HeaderActive] = COL_ROW_ACTIVE;
    c[ImGuiCol_Separator] = COL_RULE;
    c[ImGuiCol_SeparatorHovered] = COL_RULE;
    c[ImGuiCol_SeparatorActive] = COL_RULE;
    c[ImGuiCol_TextSelectedBg] = ImVec4(COL_ACCENT.x, COL_ACCENT.y, COL_ACCENT.z, 0.40f);
    c[ImGuiCol_NavCursor] = COL_ACCENT;
    c[ImGuiCol_PlotLines] = COL_ACCENT_HI;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
}

/* A font built into the DLL (res.rc), or NULL */
static ImFont* overlay_load_font(const char* name, float extra_scale)
{
    HRSRC res = FindResourceA(g_ddraw_module, name, MAKEINTRESOURCEA(10) /* RT_RCDATA */);
    HGLOBAL mem = res ? LoadResource(g_ddraw_module, res) : NULL;
    void* data = mem ? LockResource(mem) : NULL;
    DWORD size = res ? SizeofResource(g_ddraw_module, res) : 0;

    if (!data || !size)
        return NULL;

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; /* the DLL's own memory */
    cfg.ExtraSizeScale = extra_scale;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, (int)size, 13.0f, &cfg);
}

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

    /* Source Sans 3 (third_party/fonts), ImGui's own if it isn't there. Its
     * size counts the accents above and the tails below; scaled up, its
     * letters are about as tall as ImGui's own font's */
    g_font = overlay_load_font("OVERLAY_FONT", OVERLAY_FONT_SCALE);

    if (!g_font)
        g_font = io.Fonts->AddFontDefaultVector();

    /* The icons are a font of their own, drawn centred (ui_icon) */
    g_icons = overlay_load_font("OVERLAY_ICONS", 1.0f);
    io.FontDefault = g_font;

    overlay_set_style(ImGui::GetStyle());
    g_base_style = ImGui::GetStyle();
    g_style_scale = 1.0f;
}

/* Text in Source Sans 3 at a size, on a draw list, and its size */
static ImVec2 ui_text_size(float size, const char* text, float wrap = 0.0f)
{
    return g_font->CalcTextSizeA(size, FLT_MAX, wrap, text);
}

static void ui_text(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* text, float wrap = 0.0f)
{
    dl->AddText(g_font, size, ImFloor(pos), col, text, NULL, wrap);
}

/* The text, or as much of it as fits in max_w with an ellipsis (in buf) */
static const char* ui_fit(float size, const char* text, float max_w, char* buf, size_t buf_size)
{
    if (ui_text_size(size, text).x <= max_w || buf_size < 8)
        return text;

    static const char ellipsis[] = "\xe2\x80\xa6";
    size_t len = strlen(text);

    if (len > buf_size - sizeof(ellipsis))
        len = buf_size - sizeof(ellipsis);

    while (len > 0)
    {
        memcpy(buf, text, len);
        memcpy(buf + len, ellipsis, sizeof(ellipsis));

        if (ui_text_size(size, buf).x <= max_w)
            return buf;

        /* back a whole character */
        len--;

        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80)
            len--;
    }

    memcpy(buf, ellipsis, sizeof(ellipsis));
    return buf;
}

/* An icon (an ICON_ string) centred on a point */
static void ui_icon(ImDrawList* dl, const char* icon, ImVec2 center, float size, ImU32 col)
{
    if (!g_icons || !icon || !*icon)
        return;

    unsigned int cp = 0;
    ImTextCharFromUtf8(&cp, icon, NULL);

    ImFontBaked* baked = g_icons->GetFontBaked(size);
    const ImFontGlyph* glyph = baked ? baked->FindGlyphNoFallback((ImWchar)cp) : NULL;

    if (!glyph)
        return;

    float k = baked->Size > 0 ? size / baked->Size : 1.0f;
    ImVec2 pos(center.x - (glyph->X0 + glyph->X1) * 0.5f * k, center.y - (glyph->Y0 + glyph->Y1) * 0.5f * k);
    dl->AddText(g_icons, size, ImVec2(floorf(pos.x + 0.5f), floorf(pos.y + 0.5f)), col, icon, icon + strlen(icon));
}

/* ---------------------------------------------------------- the status OSD */

/* The frame times, oldest on the left, newest at the right edge */
static void overlay_draw_graph(const OVERLAY_STATUS& status, float w, float h)
{
    float sc = status.scale;

    ImGui::Dummy(ImVec2(w, h));

    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    /* the ground fades with the window's */
    dl->AddRectFilled(a, b, ui_col(COL_PILL, status.opacity), 5.0f * sc);

    float pad = 3.0f * sc;
    float x0 = a.x + pad, x1 = b.x - pad;
    float y0 = a.y + pad, y1 = b.y - pad;
    int n = status.frame_count;

    if (n > OVERLAY_HISTORY)
        n = OVERLAY_HISTORY;

    if (n >= 2 && status.graph_max_ms > 0)
    {
        ImVec2 points[OVERLAY_HISTORY];
        float step = (x1 - x0) / (OVERLAY_HISTORY - 1);
        int first = OVERLAY_HISTORY - n;

        for (int i = 0; i < n; i++)
        {
            float v = status.frame_ms[i] / status.graph_max_ms;
            v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
            points[i] = ImVec2(x0 + (first + i) * step, y1 - v * (y1 - y0));
        }

        /* Filled under the line, a quad a frame; without anti-aliasing, the
         * quads' edges would show as seams */
        ImDrawFlags flags = dl->Flags;
        dl->Flags &= ~ImDrawFlags_AAFill;
        ImU32 fill = ui_col(COL_ACCENT, 0.25f);

        for (int i = 0; i + 1 < n; i++)
            dl->AddQuadFilled(points[i], points[i + 1], ImVec2(points[i + 1].x, y1), ImVec2(points[i].x, y1), fill);

        dl->Flags = flags;
        dl->AddPolyline(points, n, ui_col(COL_ACCENT_HI), sc > 1.0f ? 1.25f * sc : 1.25f);
    }

    char scale[32];
    _snprintf(scale, sizeof(scale) - 1, "0\xe2\x80\x93%.0f ms", status.graph_max_ms);
    scale[sizeof(scale) - 1] = 0;

    float size = ImGui::GetFontSize() * 0.8f;
    ui_text(dl, size, ImVec2(a.x + 5.0f * sc, a.y + 2.0f * sc), ui_col(COL_DIM), scale);
}

static void overlay_draw_status(const OVERLAY_STATUS& status, int width, int canvas_height)
{
    float sc = status.scale;
    float margin = 10.0f * sc;
    bool right = status.corner == OVERLAY_TOP_RIGHT || status.corner == OVERLAY_BOTTOM_RIGHT;
    bool bottom = status.corner == OVERLAY_BOTTOM_LEFT || status.corner == OVERLAY_BOTTOM_RIGHT;

    ImGui::SetNextWindowPos(
        ImVec2(right ? width - margin : margin, bottom ? canvas_height - margin : margin),
        ImGuiCond_Always,
        ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));

    ImGui::SetNextWindowBgAlpha(status.opacity);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * sc, 9.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5.0f * sc, 1.5f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * sc, 3.0f * sc));

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
            float graph_width = OVERLAY_HISTORY * sc;

            if (ImGui::BeginTable("##rows", 2, ImGuiTableFlags_SizingFixedFit))
            {
                for (int i = 0; i < status.rows; i++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
                    ImGui::TextUnformatted(status.row[i].label);
                    ImGui::PopStyleColor();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(status.row[i].value);
                }

                ImGui::EndTable();

                /* as wide as the table */
                if (ImGui::GetItemRectSize().x > graph_width)
                    graph_width = ImGui::GetItemRectSize().x;
            }

            ImGui::Dummy(ImVec2(0.0f, 2.0f * sc));
            overlay_draw_graph(status, graph_width, 44.0f * sc);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

/* -------------------------------------------------------- the settings OSD */

#define MENU_TEXT 264

static bool g_menu_drawn;      /* the settings OSD was in the last frame */
static int g_tab;              /* the tab shown */
static bool g_focus_rows;      /* give the tab's rows the keyboard next */
static int g_dialog = -1;      /* the setting whose dialog is open, and how many share its row */
static int g_dialog_count;
static bool g_dialog_request;  /* open it */
static int g_capture = -1;     /* the hotkey being set */
static bool g_capture_seen;    /* its dialog was drawn this frame */
static int g_slider = -1;      /* the slider being dragged, and its value */
static int g_slider_value;
static int g_number = -1;      /* the number box being typed in, and its number */
static int g_number_value;
static int g_text = -1;        /* the text being edited, and its text */
static char g_text_value[MENU_TEXT];
static int g_custom = -1;      /* the setting whose "A number" or "Other" was picked */
static bool g_custom_focus;
static bool g_key_down[256];   /* the keys ImGui saw pressed */

static void menu_capture(int setting)
{
    g_capture = setting;
    g_capture_seen = setting >= 0; /* its dialog is on screen: it was just opened */
    overlay_menu_set_capturing(setting >= 0);

    /* The keys go to the capture now: ImGui won't see the Enter that opened
     * it let go */
    if (setting >= 0)
    {
        ImGui::GetIO().ClearInputKeys();
        memset(g_key_down, 0, sizeof(g_key_down));
    }
}

static void menu_reset(void)
{
    menu_capture(-1);
    g_dialog = -1;
    g_dialog_request = false;
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

/* Writes choice i, if it isn't the value already */
static void menu_write_choice(int setting, const SETTING* s, int choice, const char* value)
{
    char v[MENU_TEXT], l[MENU_TEXT];

    if (settings_choice(s, choice, v, sizeof(v), l, sizeof(l)) && strcmp(v, value))
        overlay_menu_write(setting, v);
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

/* A key's name; code = with its code, as "Print Screen (0x2C)" */
static void menu_key_name(int vk, char* buf, size_t size, bool code)
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

    if (vk && code)
        _snprintf(buf, size - 1, "%s (0x%02X)", name, vk);
    else
        _snprintf(buf, size - 1, "%s", name);

    buf[size - 1] = 0;
}

/* The setting whose help a setting has: a setting sharing a row (the green
 * and blue gains) has the row's first's */
static const SETTING* menu_help_of(int setting)
{
    const SETTING* s = settings_get(setting);

    while (s && !s->help && (s->flags & SETTING_JOINED) && setting > 0)
        s = settings_get(--setting);

    return s;
}

/* A line of help about one value: "value = text", the value 1 to 20 long */
static bool menu_value_line(const char* line)
{
    const char* eq = strstr(line, " = ");
    return eq && eq > line && eq - line <= 20;
}

/* The help's next line about the setting as a whole. With values = false, the
 * lines about its values are skipped; a hotkey's line ends before its " = " */
static bool menu_general_line(const SETTING* s, const char** cursor, char* buf, size_t size, bool values)
{
    while (settings_help_line(cursor, buf, size))
    {
        if (s->kind == SETTING_KEY)
        {
            char* eq = strstr(buf, " = ");

            if (eq)
                *eq = 0;

            return true;
        }

        if (values || !menu_value_line(buf))
            return true;
    }

    return false;
}

/* What a row says under its name: the first line of the help, not a note */
static bool menu_description(int setting, char* buf, size_t size)
{
    const SETTING* s = menu_help_of(setting);
    const char* cursor = s && s->help ? s->help : "";

    const SETTING* own = settings_get(setting);
    const char* title = own->label ? own->label : own->key;

    while (menu_general_line(s, &cursor, buf, size, false))
    {
        if (strncmp(buf, "Note:", 5))
            return _stricmp(buf, title) != 0; /* Not one that only says the name again */
    }

    return false;
}

/* A value as the OSD shows it */
static void menu_value_label(const SETTING* s, const char* value, char* buf, size_t size)
{
    if (s->kind == SETTING_KEY)
    {
        menu_key_name((int)strtol(value, NULL, 0), buf, size, false);
        return;
    }

    if (s->choices && menu_find_choice(s, value, buf, size) >= 0)
        return;

    /* A value the OSD doesn't name (DOSBox's "auto" for the colour profile) is the default */
    if (s->kind == SETTING_CHOICE && menu_find_choice(s, s->value, buf, size) >= 0)
        return;

    _snprintf(buf, size - 1, "%s", value[0] ? value : "(empty)");
    buf[size - 1] = 0;
}

/* Adds a value to a list of them, as the OSD shows it */
static void menu_append_value(char* buf, size_t size, const SETTING* s, const char* value)
{
    char text[MENU_TEXT];
    menu_value_label(s, value, text, sizeof(text));

    size_t len = strlen(buf);

    if (len + 1 < size)
    {
        _snprintf(buf + len, size - len - 1, "%s%s", len ? ", " : "", text);
        buf[size - 1] = 0;
    }
}

/* The colour of a setting for one colour (the RGB gains), and its name; or NULL */
static const ImVec4* menu_tint(const SETTING* s, const char** name)
{
    static const struct
    {
        const char* key;
        const char* name;
        ImVec4 color;
    } tints[] = {
        { "red_gain", "Red", ImVec4(0.86f, 0.30f, 0.30f, 1.0f) },
        { "green_gain", "Green", ImVec4(0.35f, 0.75f, 0.38f, 1.0f) },
        { "blue_gain", "Blue", ImVec4(0.40f, 0.56f, 0.96f, 1.0f) },
    };

    for (size_t i = 0; i < sizeof(tints) / sizeof(tints[0]); i++)
    {
        if (!strcmp(s->key, tints[i].key))
        {
            if (name)
                *name = tints[i].name;

            return &tints[i].color;
        }
    }

    return NULL;
}

/* The icon of a setting ("name*" = the keys starting with it) */
static const char* menu_setting_icon(const char* key)
{
    static const struct
    {
        const char* key;
        const char* icon;
    } icons[] = {
        { "display", ICON_FULLSCREEN },
        { "keytogglefullscreen*", ICON_FULLSCREEN },
        { "window_size", ICON_PHOTO_SIZE_SELECT_LARGE },
        { "window_position", ICON_OPEN_WITH },
        { "window_decorations", ICON_WEB_ASSET },
        { "window_resizable", ICON_OPEN_IN_FULL },
        { "keytogglemaximize*", ICON_OPEN_IN_FULL },
        { "remember_window", ICON_SAVE },
        { "renderer", ICON_MEMORY },
        { "maxfps", ICON_SPEED },
        { "vsync", ICON_SYNC },
        { "emulated_refresh", ICON_AV_TIMER },
        { "adjmouse", ICON_MOUSE },
        { "unlockcursor_middleclick", ICON_MOUSE },
        { "shader", ICON_GRADIENT },
        { "aspect", ICON_ASPECT_RATIO },
        { "integer_scaling", ICON_GRID_ON },
        { "viewport", ICON_CROP_FREE },
        { "image_adjustments", ICON_TONALITY },
        { "crt_color_profile", ICON_TV },
        { "brightness", ICON_BRIGHTNESS_6 },
        { "contrast", ICON_CONTRAST },
        { "gamma", ICON_EXPOSURE },
        { "color_temperature", ICON_THERMOSTAT },
        { "luma_preserve", ICON_BRIGHTNESS_MEDIUM },
        { "red_gain", ICON_COLOR_LENS },
        { "digital_contrast", ICON_FILTER_B_AND_W },
        { "saturation", ICON_INVERT_COLORS },
        { "black_level", ICON_BRIGHTNESS_LOW },
        { "devmode", ICON_CODE },
        { "anti_aliased_fonts_min_size", ICON_TEXT_FIELDS },
        { "screenshotdir", ICON_FOLDER },
        { "status_osd", ICON_INSIGHTS },
        { "keystatusosd", ICON_INSIGHTS },
        { "status_osd_size", ICON_FORMAT_SIZE },
        { "status_osd_detail", ICON_NOTES },
        { "status_osd_position", ICON_PICTURE_IN_PICTURE },
        { "status_osd_opacity", ICON_OPACITY },
        { "noactivateapp", ICON_VISIBILITY_OFF },
        { "maxgameticks", ICON_SLOW_MOTION_VIDEO },
        { "limiter_type", ICON_TIMELAPSE },
        { "minfps", ICON_HOURGLASS_BOTTOM },
        { "nonexclusive", ICON_FULLSCREEN_EXIT },
        { "singlecpu", ICON_DEVELOPER_BOARD },
        { "resolutions", ICON_VIEW_LIST },
        { "fixchilds", ICON_FILTER_NONE },
        { "hook_peekmessage", ICON_CENTER_FOCUS_STRONG },
        { "windowed_bpp", ICON_COLORIZE },
        { "keyunlockcursor*", ICON_LOCK_OPEN },
        { "keyscreenshot", ICON_PHOTO_CAMERA },
        { "keysettingsosd", ICON_SETTINGS },
    };

    for (size_t i = 0; key && i < sizeof(icons) / sizeof(icons[0]); i++)
    {
        size_t len = strlen(icons[i].key);

        if (icons[i].key[len - 1] == '*' ? !strncmp(key, icons[i].key, len - 1) : !strcmp(key, icons[i].key))
            return icons[i].icon;
    }

    return ICON_TUNE;
}

static const char* menu_tab_icon(int tab)
{
    static const char* icons[] = {
        ICON_DESKTOP_WINDOWS, ICON_IMAGE, ICON_PALETTE, ICON_TIMER, ICON_LAYERS, ICON_TUNE, ICON_BUILD, ICON_KEYBOARD,
    };

    return tab >= 0 && tab < (int)(sizeof(icons) / sizeof(icons[0])) ? icons[tab] : ICON_TUNE;
}

/* A switch, on or off, at pos */
static void ui_switch(ImDrawList* dl, ImVec2 pos, float w, float h, bool on, bool hovered)
{
    float r = h * 0.5f;
    ImVec4 track = on ? COL_ACCENT : COL_TRACK;

    if (hovered)
        track = on ? COL_ACCENT_HI : ImVec4(0.36f, 0.37f, 0.40f, 1.0f);

    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), ui_col(track), r);

    float knob = r - (h * 0.14f > 1.5f ? h * 0.14f : 1.5f);
    ImVec2 c(on ? pos.x + w - r : pos.x + r, pos.y + r);
    dl->AddCircleFilled(c, knob, ui_col(COL_KNOB), 24);
}

/* A square button with an icon on it, hovered or 'active' brighter */
static bool menu_icon_button(const char* id, const char* icon, float size, float icon_size, bool active)
{
    bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (hovered || held)
        dl->AddRectFilled(a, b, ui_col(held ? COL_ROW_ACTIVE : COL_ROW_HOVER), size * 0.22f);

    ui_icon(dl, icon, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), icon_size,
        ui_col(active || hovered ? COL_TEXT : COL_DIM));

    return pressed;
}

/* The help of a setting in a tooltip, when its row has been under the mouse a while */
static void menu_tooltip(int setting, float F)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_Stationary |
                              ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_NoSharedDelay))
        return;

    const SETTING* s = menu_help_of(setting);
    const char* cursor = s && s->help ? s->help : "";
    char line[1024];

    if (!*cursor)
        return;

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(30.0f * F);

    while (menu_general_line(s, &cursor, line, sizeof(line), true))
        ImGui::TextUnformatted(line);

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/* Opens the dialog of a row: settings first to first + count - 1 */
static void menu_open_dialog(int first, int count)
{
    const SETTING* s = settings_get(first);

    g_dialog = first;
    g_dialog_count = count;
    g_dialog_request = true;
    g_custom = -1;
    g_slider = -1;
    g_number = -1;
    g_text = -1;

    if (s->kind == SETTING_KEY)
        menu_capture(first);

    /* Text of its own only: the dialog is the text field */
    if (s->kind == SETTING_STRING && !s->choices)
    {
        g_custom = first;
        g_custom_focus = true;
    }
}

/* A row: its icon, its name and what it is, and its value (a switch, or what
 * opens its dialog) on the right; settings first to first + count - 1 */
static void menu_row(int first, int count, float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return;

    const SETTING* s = settings_get(first);
    ImDrawList* dl = window->DrawList;
    ImGuiID id = window->GetID("##row");

    float pad_x = 0.7f * F, pad_y = 0.55f * F;
    float icon_w = 1.9f * F;
    float title_size = 1.12f * F, desc_size = 0.88f * F, value_size = 1.0f * F;
    float rounding = 0.4f * F;

    ImVec2 pos = window->DC.CursorPos;
    float w = ImGui::GetContentRegionAvail().x;

    char value[MENU_TEXT];
    strncpy(value, overlay_menu_value(first, NULL), sizeof(value) - 1);
    value[sizeof(value) - 1] = 0;

    bool is_bool = s->kind == SETTING_BOOL && count == 1;
    int on = 0;

    if (is_bool)
        on = ds_parse_bool(value, &on) && on;

    /* The value: a switch, or a pill with its value (the joined ones' in their colours) */
    struct
    {
        char text[MENU_TEXT];
        ImU32 col;
        float w;
    } parts[3];

    int n_parts = 0;
    float sep_w = ui_text_size(value_size, "  ").x;
    float value_w, pill_pad = 0.65f * F, chevron_w = 1.1f * F;

    if (is_bool)
    {
        value_w = 2.5f * F;
    }
    else
    {
        float text_w = 0;

        for (int m = first; m < first + count && n_parts < 3; m++)
        {
            const SETTING* ms = settings_get(m);
            menu_value_label(ms, overlay_menu_value(m, NULL), parts[n_parts].text, sizeof(parts[n_parts].text));

            const ImVec4* tint = menu_tint(ms, NULL);
            ImVec4 c = COL_TEXT;

            if (tint && count > 1)
                c = ImVec4(tint->x * 0.5f + 0.5f, tint->y * 0.5f + 0.5f, tint->z * 0.5f + 0.5f, 1.0f);

            parts[n_parts].col = ui_col(c);
            parts[n_parts].w = ui_text_size(value_size, parts[n_parts].text).x;
            text_w += parts[n_parts].w + (n_parts ? sep_w : 0.0f);
            n_parts++;
        }

        value_w = pill_pad + text_w + chevron_w;

        /* At most 45% of the row; the text is cut to fit */
        if (value_w > w * 0.45f)
        {
            value_w = w * 0.45f;

            if (n_parts == 1)
            {
                char cut[MENU_TEXT];

                if (ui_fit(value_size, parts[0].text, value_w - pill_pad - chevron_w, cut, sizeof(cut)) == cut)
                    strcpy(parts[0].text, cut);
            }
        }
    }

    /* The name and what it is */
    const char* title = s->label ? s->label : s->key;
    bool restart = s->apply == SETTING_RESTART;
    float tag_size = 0.72f * F;
    const char* tag = "restart";
    float tag_w = restart ? ui_text_size(tag_size, tag).x + 0.7f * F : 0.0f;

    float text_x = pos.x + pad_x + icon_w;
    float text_w = pos.x + w - pad_x - value_w - 0.8f * F - text_x;

    if (text_w < 4.0f * F)
        text_w = 4.0f * F;

    char title_buf[MENU_TEXT];
    const char* title_fit = ui_fit(title_size, title, text_w - (restart ? tag_w + 0.4f * F : 0.0f), title_buf, sizeof(title_buf));
    ImVec2 title_sz = ui_text_size(title_size, title_fit);

    char desc[1024];
    bool has_desc = menu_description(first, desc, sizeof(desc));
    ImVec2 desc_sz = has_desc ? ui_text_size(desc_size, desc, text_w) : ImVec2(0, 0);
    float gap = has_desc ? 0.12f * F : 0.0f;

    float content_h = title_sz.y + gap + desc_sz.y;
    float h = (content_h > 2.0f * F ? content_h : 2.0f * F) + 2 * pad_y;

    ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bb);

    if (!ImGui::ItemAdd(bb, id))
        return;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    ImGuiContext& g = *GImGui;
    bool nav = g.NavId == id && g.NavCursorVisible;

    if (held && hovered)
        dl->AddRectFilled(bb.Min, bb.Max, ui_col(COL_ROW_ACTIVE), rounding);
    else if (hovered || nav)
        dl->AddRectFilled(bb.Min, bb.Max, ui_col(COL_ROW_HOVER), rounding);

    ImGui::RenderNavCursor(bb, id, ImGuiNavRenderCursorFlags_None, rounding);

    float cy = pos.y + h * 0.5f;
    bool changed = false;

    for (int m = first; m < first + count; m++)
        changed = changed || menu_changed(m);

    /* A dot: not the default */
    if (changed)
        dl->AddCircleFilled(ImVec2(pos.x + pad_x * 0.5f, cy), 0.16f * F, ui_col(COL_ACCENT), 12);

    ui_icon(dl, menu_setting_icon(s->key), ImVec2(pos.x + pad_x + icon_w * 0.4f, cy), 1.35f * F, ui_col(COL_HEADING));

    float ty = pos.y + (h - content_h) * 0.5f;
    ui_text(dl, title_size, ImVec2(text_x, ty), ui_col(COL_TEXT), title_fit);

    if (restart)
    {
        float th = ui_text_size(tag_size, tag).y;
        ImVec2 a(text_x + title_sz.x + 0.4f * F, ty + (title_sz.y - th) * 0.5f - 0.08f * F);
        ImVec2 b(a.x + tag_w, a.y + th + 0.16f * F);
        dl->AddRect(a, b, ui_col(COL_RULE), 0.3f * F, 1.0f);
        ui_text(dl, tag_size, ImVec2(a.x + 0.35f * F, a.y + 0.08f * F), ui_col(COL_DIM), tag);
    }

    if (has_desc)
        ui_text(dl, desc_size, ImVec2(text_x, ty + title_sz.y + gap), ui_col(COL_DIM), desc, text_w);

    /* The value */
    float vx = pos.x + w - pad_x - value_w;

    if (is_bool)
    {
        float sh = 1.35f * F;
        ui_switch(dl, ImVec2(vx, cy - sh * 0.5f), value_w, sh, on != 0, hovered || nav);
    }
    else
    {
        float ph = 2.0f * F;
        ImVec2 a(vx, cy - ph * 0.5f), b(vx + value_w, cy + ph * 0.5f);
        dl->AddRectFilled(a, b, ui_col(hovered || nav ? COL_PILL_HOVER : COL_PILL), 0.45f * F);

        float x = a.x + pill_pad;
        float vh = ui_text_size(value_size, "A").y;
        ImVec4 clip(a.x, a.y, b.x - chevron_w, b.y);

        for (int i = 0; i < n_parts; i++)
        {
            if (i)
                x += sep_w;

            dl->AddText(g_font, value_size, ImFloor(ImVec2(x, cy - vh * 0.5f)), parts[i].col, parts[i].text, NULL, 0.0f, &clip);
            x += parts[i].w;
        }

        ui_icon(dl, ICON_CHEVRON_RIGHT, ImVec2(b.x - chevron_w * 0.5f, cy), 1.2f * F, ui_col(COL_DIM));
    }

    if (is_bool)
        menu_tooltip(first, F);

    if (pressed)
    {
        if (is_bool)
        {
            char off[32], on_word[32], label[32];
            settings_choice(s, 0, off, sizeof(off), label, sizeof(label));
            settings_choice(s, 1, on_word, sizeof(on_word), label, sizeof(label));
            overlay_menu_write(first, on ? off : on_word);
        }
        else
        {
            menu_open_dialog(first, count);
        }
    }
}

/* A heading between a tab's rows; its help in a tooltip */
static void menu_heading(const SETTING* s, float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return;

    float size = 0.95f * F;
    const char* text = s->label ? s->label : "";
    ImVec2 sz = ui_text_size(size, text);
    float above = window->DC.CursorPos.y > window->DC.CursorStartPos.y + 1.0f ? 0.9f * F : 0.2f * F;

    ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + above);
    float w = ImGui::GetContentRegionAvail().x;
    ImRect bb(window->DC.CursorPos, ImVec2(pos.x + w, pos.y + sz.y + 0.35f * F));
    ImGui::ItemSize(bb);

    if (!ImGui::ItemAdd(bb, 0))
        return;

    ImDrawList* dl = window->DrawList;
    float x = pos.x + 0.7f * F;
    ui_text(dl, size, ImVec2(x, pos.y), ui_col(COL_HEADING), text);

    float ly = floorf(pos.y + sz.y * 0.55f) + 0.5f;
    float lx = x + sz.x + 0.6f * F;

    if (lx < pos.x + w - 0.7f * F)
        dl->AddLine(ImVec2(lx, ly), ImVec2(pos.x + w - 0.7f * F, ly), ui_col(COL_RULE), 1.0f);

    if (s->help && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_NoSharedDelay))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(30.0f * F);
        ImGui::TextUnformatted(s->help);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/* A tab's rows between its headings. A setting marked SETTING_JOINED shares
 * the row of the one before */
static void menu_rows(int tab, float F)
{
    const char* name = settings_tab(tab);
    bool gated = false; /* a SETTING_GATE before is off */

    for (int i = 0; name && i < settings_count(); i++)
    {
        const SETTING* s = settings_get(i);

        if (!s->tab || strcmp(s->tab, name))
            continue;

        if (s->kind == SETTING_HEADING)
        {
            menu_heading(s, F);
            continue;
        }

        if (!s->key || (s->flags & SETTING_JOINED))
            continue;

        int count = 1;

        while (i + count < settings_count() && (settings_get(i + count)->flags & SETTING_JOINED))
            count++;

        ImGui::PushID(i);
        ImGui::BeginDisabled(gated);
        menu_row(i, count, F);
        ImGui::EndDisabled();
        ImGui::PopID();

        for (int m = i; m < i + count; m++)
        {
            if (settings_get(m)->flags & SETTING_GATE)
            {
                int on = 0;
                gated = !(ds_parse_bool(overlay_menu_value(m, NULL), &on) && on);
            }
        }

        i += count - 1;
    }
}

/* ------------------------------------------------- the settings OSD's dialog */

#define MENU_OTHER -2 /* a value of its own: "A number" or "Other" */

/* A choice in a dialog: a tick when it's the one, its name, and its help */
static bool menu_choice_row(int choice, const char* label, const char* help, bool selected, float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return false;

    ImGui::PushID(choice);
    ImGuiID id = window->GetID("##choice");
    ImGui::PopID();

    float pad_y = 0.4f * F, check_w = 1.8f * F;
    float label_size = 1.05f * F, help_size = 0.88f * F;
    ImVec2 pos = window->DC.CursorPos;
    float w = ImGui::GetContentRegionAvail().x;
    float text_x = pos.x + check_w;
    float text_w = pos.x + w - 0.6f * F - text_x;

    if (help && (!*help || !_stricmp(help, label)))
        help = NULL;

    ImVec2 label_sz = ui_text_size(label_size, label, text_w);
    ImVec2 help_sz = help ? ui_text_size(help_size, help, text_w) : ImVec2(0, 0);
    float gap = help ? 0.08f * F : 0.0f;
    float h = label_sz.y + gap + help_sz.y + 2 * pad_y;

    if (h < 2.0f * F)
        h = 2.0f * F;

    ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bb);

    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    ImGuiContext& g = *GImGui;
    ImDrawList* dl = window->DrawList;
    float rounding = 0.4f * F;

    if (held && hovered)
        dl->AddRectFilled(bb.Min, bb.Max, ui_col(COL_ROW_ACTIVE), rounding);
    else if (hovered || (g.NavId == id && g.NavCursorVisible))
        dl->AddRectFilled(bb.Min, bb.Max, ui_col(COL_ROW_HOVER), rounding);

    ImGui::RenderNavCursor(bb, id, ImGuiNavRenderCursorFlags_None, rounding);

    float content_h = label_sz.y + gap + help_sz.y;
    float ty = pos.y + (h - content_h) * 0.5f;

    if (selected)
        ui_icon(dl, ICON_CHECK, ImVec2(pos.x + check_w * 0.5f, ty + label_sz.y * 0.5f), 1.3f * F, ui_col(COL_ACCENT_HI));

    ui_text(dl, label_size, ImVec2(text_x, ty), ui_col(selected ? COL_ACCENT_HI : COL_TEXT), label, text_w);

    if (help)
        ui_text(dl, help_size, ImVec2(text_x, ty + label_sz.y + gap), ui_col(COL_DIM), help, text_w);

    if (selected)
        ImGui::SetItemDefaultFocus();

    return pressed;
}

/* A slider and a box to type the number in, the rest of the line. The slider
 * moves in the setting's steps; a number typed in needn't be one */
static void menu_number(int setting, const SETTING* s, float F)
{
    char value[MENU_TEXT];
    strncpy(value, overlay_menu_value(setting, NULL), sizeof(value) - 1);
    value[sizeof(value) - 1] = 0;

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
    const char* tint_name = NULL;
    const ImVec4* tint = menu_tint(s, &tint_name);

    /* The joined ones are told apart by their colours' names */
    if (tint && (s->flags & SETTING_JOINED || (setting + 1 < settings_count() && settings_get(setting + 1)->flags & SETTING_JOINED)))
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(tint->x * 0.5f + 0.5f, tint->y * 0.5f + 0.5f, tint->z * 0.5f + 0.5f, 1.0f), "%s", tint_name);
        ImGui::SameLine(4.0f * F);
    }

    char lo[16], hi[16];
    _snprintf(lo, sizeof(lo) - 1, "%d", s->min);
    _snprintf(hi, sizeof(hi) - 1, "%d", s->max);
    lo[sizeof(lo) - 1] = hi[sizeof(hi) - 1] = 0;

    /* The box fits the longest number in the range, and the cursor */
    float lo_width = ImGui::CalcTextSize(lo).x;
    float hi_width = ImGui::CalcTextSize(hi).x;
    float box = (lo_width > hi_width ? lo_width : hi_width) + ImGui::CalcTextSize("00").x + style.FramePadding.x * 2;
    float slider = ImGui::GetContentRegionAvail().x - box - style.ItemSpacing.x;

    if (slider < ImGui::GetFrameHeight())
        slider = ImGui::GetFrameHeight();

    ImVec4 c = tint ? *tint : COL_ACCENT;
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, c);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(c.x * 0.5f + 0.5f, c.y * 0.5f + 0.5f, c.z * 0.5f + 0.5f, 1.0f));

    if (tint)
    {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.x, c.y, c.z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(c.x, c.y, c.z, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(c.x, c.y, c.z, 0.42f));
    }

    int old = v;
    ImGui::PushID(setting);
    ImGui::SetNextItemWidth(slider);

    if (ImGui::SliderInt("##int", &v, s->min, s->max, "", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
    {
        g_slider = setting;
        g_slider_value = menu_clamp(s, menu_snap(v, old, s->step, ImGui::IsMouseDown(ImGuiMouseButton_Left)));
        v = g_slider_value;
    }

    ImGui::PopStyleColor(tint ? 5 : 2);

    /* Written when it's let go: a picture setting sets the picture up again */
    if (ImGui::IsItemDeactivated() && g_slider == setting)
    {
        menu_write_int(setting, value, g_slider_value);
        g_slider = -1;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(box);

    /* While it's typed in, ImGui keeps the text; the number is written when it's left */
    int n = v;

    if (ImGui::InputInt("##number", &n, 0, 0))
    {
        g_number = setting;
        g_number_value = n;
    }

    if (ImGui::IsItemDeactivated() && g_number == setting)
    {
        if (ImGui::IsItemDeactivatedAfterEdit())
            menu_write_int(setting, value, menu_clamp(s, g_number_value));

        g_number = -1;
    }

    ImGui::PopID();
}

/* A text field, the width of the dialog. Returns true when Enter is pressed */
static bool menu_text_field(int setting)
{
    char value[MENU_TEXT];
    strncpy(value, overlay_menu_value(setting, NULL), sizeof(value) - 1);
    value[sizeof(value) - 1] = 0;

    char buf[MENU_TEXT];
    strncpy(buf, g_text == setting ? g_text_value : value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    if (g_custom == setting && g_custom_focus)
    {
        ImGui::SetKeyboardFocusHere();
        g_custom_focus = false;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    bool enter = ImGui::InputText("##text", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);

    if (ImGui::IsItemActive())
    {
        g_text = setting;
        strcpy(g_text_value, buf);
    }

    if (enter || (ImGui::IsItemDeactivated() && g_text == setting))
    {
        g_text = -1;

        /* Leading and trailing spaces would be lost in the ini anyway */
        char* start = buf;
        while (*start == ' ')
            start++;

        size_t len = strlen(start);
        while (len > 0 && start[len - 1] == ' ')
            start[--len] = 0;

        if ((enter || ImGui::IsItemDeactivatedAfterEdit()) && strcmp(start, value))
        {
            /* Nothing = the default */
            overlay_menu_write(setting, start[0] ? start : NULL);
        }
    }

    return enter;
}

static void menu_dialog_closed(void)
{
    if (g_capture >= 0)
        menu_capture(-1);

    g_dialog = -1;
    g_dialog_request = false;
    g_custom = -1;
    g_slider = -1;
    g_number = -1;
    g_text = -1;
}

/* A button, accent = the one to press */
static bool menu_button(const char* label, float w, bool accent)
{
    if (accent)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, COL_ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_HI);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, COL_ACCENT);
    }

    bool pressed = ImGui::Button(label, ImVec2(w, 0));

    if (accent)
        ImGui::PopStyleColor(3);

    return pressed;
}

/* The dialog of the row clicked: the help, and its choices, a number, a text
 * or a key; its default and Reset at the bottom */
static void menu_dialog(int width, float canvas_h, float F)
{
    if (g_dialog_request)
    {
        ImGui::OpenPopup("##dialog");
        g_dialog_request = false;
    }

    if (g_dialog < 0)
    {
        /* Left open when the OSD was closed */
        if (ImGui::IsPopupOpen("##dialog") && ImGui::BeginPopupModal("##dialog"))
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        return;
    }

    if (!ImGui::IsPopupOpen("##dialog"))
    {
        menu_dialog_closed();
        return;
    }

    int first = g_dialog, count = g_dialog_count;
    const SETTING* s = settings_get(first);

    float dw = 36.0f * F;

    if (dw > width - 2.0f * F)
        dw = width - 2.0f * F;

    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, canvas_h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(dw, 0.0f), ImVec2(dw, canvas_h * 0.9f));
    /* Whole pixels: ImGui truncates the fitted size, and a fraction short of the content shows a scrollbar */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImFloor(1.1f * F), ImFloor(0.9f * F)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.5f * F, 0.45f * F));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.6f * F, 0.4f * F));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginPopupModal("##dialog", NULL, flags))
    {
        bool close = false;
        bool capturing = g_capture == first;

        if (capturing)
            g_capture_seen = true;

        /* Esc closes it, unless it's for a text field; and a click outside it */
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && g_capture < 0 && !ImGui::IsAnyItemActive())
            close = true;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            close = true;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImGuiStyle& style = ImGui::GetStyle();
        float iw = ImGui::GetContentRegionAvail().x;
        char text[1024];

        /* The icon, the name, and a close button */
        float hh = 2.2f * F;
        ImGui::Dummy(ImVec2(iw - hh - style.ItemSpacing.x, hh));
        ImVec2 p = ImGui::GetItemRectMin();
        ui_icon(dl, menu_setting_icon(s->key), ImVec2(p.x + 0.7f * F, p.y + hh * 0.5f), 1.5f * F, ui_col(COL_ACCENT_HI));

        float title_size = 1.25f * F;
        const char* title = ui_fit(title_size, s->label ? s->label : s->key, iw - hh - 2.4f * F, text, sizeof(text));
        ui_text(dl, title_size, ImVec2(p.x + 1.9f * F, p.y + (hh - ui_text_size(title_size, title).y) * 0.5f), ui_col(COL_TEXT), title);

        ImGui::SameLine();

        if (menu_icon_button("##close", ICON_CLOSE, hh, 1.3f * F, false))
            close = true;

        /* What it is: the help, without the lines about the choices shown below */
        const SETTING* help = menu_help_of(first);
        const char* cursor = help && help->help ? help->help : "";
        bool listed = s->choices && s->kind != SETTING_BOOL && s->kind != SETTING_KEY && count == 1;

        ImGui::PushFont(NULL, 0.95f * F);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
        ImGui::PushTextWrapPos(0.0f);

        while (menu_general_line(help, &cursor, text, sizeof(text), !listed))
            ImGui::TextUnformatted(text);

        ImGui::PopStyleColor();

        if (s->apply == SETTING_RESTART)
            ImGui::TextColored(COL_ACCENT_HI, "Takes effect when the game starts again");

        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 0.2f * F));

        char value[MENU_TEXT];
        strncpy(value, overlay_menu_value(first, NULL), sizeof(value) - 1);
        value[sizeof(value) - 1] = 0;

        bool number = s->kind == SETTING_INT;
        bool editor = (s->kind == SETTING_INT || s->kind == SETTING_STRING) && (!s->choices || count > 1);

        /* The choices, and "A number" or "Other" */
        if (listed)
        {
            int current = menu_find_choice(s, value, NULL, 0);

            if (current < 0 && s->kind == SETTING_CHOICE)
                current = menu_find_choice(s, s->value, NULL, 0);

            bool own = s->kind != SETTING_CHOICE;
            bool on_own = own && (current < 0 || g_custom == first);
            char v[MENU_TEXT], l[MENU_TEXT], h[1024];

            for (int i = 0; settings_choice(s, i, v, sizeof(v), l, sizeof(l)); i++)
            {
                bool has_help = settings_choice_help(s, v, h, sizeof(h)) != 0;

                if (menu_choice_row(i, l, has_help ? h : NULL, !on_own && current == i, F))
                {
                    g_custom = -1;
                    menu_write_choice(first, s, i, value);
                    close = true;
                }
            }

            if (own)
            {
                bool has_help = settings_choice_help(s, NULL, h, sizeof(h)) != 0;

                if (menu_choice_row(MENU_OTHER, number ? "A number" : "Other", has_help ? h : NULL, on_own, F) && !on_own)
                {
                    g_custom = first;
                    g_custom_focus = true;
                }

                editor = on_own || g_custom == first;
            }
        }

        if (editor)
        {
            ImGui::Dummy(ImVec2(0.0f, 0.1f * F));

            if (number)
            {
                for (int m = first; m < first + count; m++)
                    menu_number(m, settings_get(m), F);
            }
            else if (menu_text_field(first))
            {
                close = true;
            }
        }

        /* A hotkey: what's pressed next */
        if (s->kind == SETTING_KEY)
        {
            float bh = 3.0f * F;
            ImGui::Dummy(ImVec2(iw, bh));
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            dl->AddRectFilled(a, b, ui_col(COL_PILL), 0.45f * F);

            float pulse = 0.55f + 0.45f * sinf((float)ImGui::GetTime() * 5.0f);

            if (capturing)
                dl->AddRect(a, b, ui_col(COL_ACCENT, pulse), 0.45f * F, 0.12f * F > 1.0f ? 0.12f * F : 1.0f);

            if (capturing)
            {
                strcpy(text, "Press a key");
            }
            else
            {
                menu_key_name((int)strtol(value, NULL, 0), text, sizeof(text), true);
            }

            float size = 1.15f * F;
            ImVec2 sz = ui_text_size(size, text);
            ui_text(dl, size, ImVec2((a.x + b.x - sz.x) * 0.5f, (a.y + b.y - sz.y) * 0.5f), ui_col(COL_TEXT), text);

            if (capturing)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
                ImGui::TextUnformatted("Esc cancels");
                ImGui::PopStyleColor();
            }

            /* A key was pressed, or Esc */
            if (!capturing)
                close = true;
        }

        /* The default, and the buttons */
        ImGui::Dummy(ImVec2(0.0f, 0.2f * F));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 0.1f * F));

        bool changed = false;
        char defaults[MENU_TEXT * 2] = "";

        for (int m = first; m < first + count; m++)
        {
            changed = changed || menu_changed(m);
            menu_append_value(defaults, sizeof(defaults), settings_get(m), settings_get(m)->value);
        }

        const char* labels[3];
        int n_buttons = 0;

        if (s->kind == SETTING_KEY)
        {
            labels[n_buttons++] = "None";
            labels[n_buttons++] = "Reset";
            labels[n_buttons++] = "Cancel";
        }
        else
        {
            labels[n_buttons++] = "Reset";
            labels[n_buttons++] = "Done";
        }

        float bw = 5.0f * F;

        for (int i = 0; i < n_buttons; i++)
        {
            float w = ImGui::CalcTextSize(labels[i]).x + style.FramePadding.x * 2;

            if (w > bw)
                bw = w;
        }

        float buttons_w = bw * n_buttons + style.ItemSpacing.x * (n_buttons - 1);
        char line[MENU_TEXT * 2 + 16], cut[MENU_TEXT * 2 + 16];
        _snprintf(line, sizeof(line) - 1, "Default: %s", defaults);
        line[sizeof(line) - 1] = 0;

        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
        ImGui::TextUnformatted(ui_fit(ImGui::GetFontSize(), line, iw - buttons_w - style.ItemSpacing.x * 2, cut, sizeof(cut)));
        ImGui::PopStyleColor();
        ImGui::SameLine(iw - buttons_w + style.WindowPadding.x);

        for (int i = 0; i < n_buttons; i++)
        {
            const char* label = labels[i];
            bool reset = !strcmp(label, "Reset");

            if (i)
                ImGui::SameLine();

            ImGui::BeginDisabled(reset && !changed);

            if (menu_button(label, bw, !strcmp(label, "Done")))
            {
                if (!strcmp(label, "None"))
                {
                    menu_capture(-1);
                    overlay_menu_write(first, "0x00");
                }
                else if (reset)
                {
                    if (g_capture >= 0)
                        menu_capture(-1);

                    for (int m = first; m < first + count; m++)
                    {
                        if (menu_changed(m))
                            overlay_menu_write(m, settings_get(m)->value);
                    }

                    g_slider = g_number = g_text = -1;
                }

                /* Reset leaves a number or a text open, to go on from the default */
                if (!reset || s->kind == SETTING_KEY || listed)
                    close = true;
            }

            ImGui::EndDisabled();
        }

        if (close)
        {
            ImGui::CloseCurrentPopup();
            menu_dialog_closed();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(3);
}

/* -------------------------------------------------- the settings OSD's panel */

/* A key and what it does, in the footer, drawn right to left from x;
 * returns where the next one goes, or -1 when it didn't fit after min_x */
static float menu_hint(ImDrawList* dl, float x, float cy, float min_x, const char* key, const char* what, float F)
{
    float key_size = 0.82f * F, what_size = 0.88f * F;
    ImVec2 ks = ui_text_size(key_size, key);
    ImVec2 ws = ui_text_size(what_size, what);
    float cap_w = ks.x + 0.7f * F, cap_h = ks.y + 0.2f * F;
    float w = cap_w + 0.45f * F + ws.x;

    if (x - w < min_x)
        return -1.0f;

    float left = x - w;
    ImVec2 a(left, cy - cap_h * 0.5f), b(left + cap_w, cy + cap_h * 0.5f);
    dl->AddRectFilled(a, b, ui_col(COL_PILL), 0.3f * F);
    dl->AddRect(a, b, ui_col(COL_RULE), 0.3f * F, 1.0f);
    ui_text(dl, key_size, ImVec2(a.x + 0.35f * F, cy - ks.y * 0.5f), ui_col(COL_TEXT), key);
    ui_text(dl, what_size, ImVec2(b.x + 0.45f * F, cy - ws.y * 0.5f), ui_col(COL_DIM), what);

    return left - 1.2f * F;
}

/* The settings OSD: a panel down the left, the picture still showing to its right */
static void menu_draw(int width, int canvas_height, float F)
{
    float ch = (float)canvas_height;
    float pw = width * 0.42f;

    if (pw < 34.0f * F)
        pw = 34.0f * F;

    if (pw > 52.0f * F)
        pw = 52.0f * F;

    if (pw > width)
        pw = (float)width;

    pw = floorf(pw);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pw, ch), ImGuiCond_Always);

    if (!g_menu_drawn)
    {
        ImGui::SetNextWindowFocus();
        g_focus_rows = true;
    }

    bool open = true;
    g_capture_seen = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;

    bool shown = ImGui::Begin("##settingsosd", NULL, flags);
    ImGui::PopStyleVar(3);

    if (shown)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int tabs = 0;

        while (settings_tab(tabs))
            tabs++;

        if (g_tab >= tabs)
            g_tab = 0;

        bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

        /* Esc closes it, unless it's for a text field or a dialog; Left and Right change the page */
        if (!popup && g_capture < 0 && !ImGui::IsAnyItemActive())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                open = false;

            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && tabs)
            {
                g_tab = (g_tab + tabs - 1) % tabs;
                g_focus_rows = true;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && tabs)
            {
                g_tab = (g_tab + 1) % tabs;
                g_focus_rows = true;
            }
        }

        /* The keyboard goes to the rows, not the panel around them */
        if (ImGui::IsWindowFocused() && !popup)
            g_focus_rows = true;

        /* The header: the page's name, and the pages' icons */
        float hh = floorf(3.4f * F);
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(pw, hh), ui_col(COL_BAR));
        dl->AddLine(ImVec2(0, hh - 0.5f), ImVec2(pw, hh - 0.5f), ui_col(COL_RULE));

        float bs = 2.2f * F;
        float by = (hh - bs) * 0.5f;
        float x = pw - 0.5f * F - bs;

        ImGui::SetCursorPos(ImVec2(x, by));

        if (menu_icon_button("##close", ICON_CLOSE, bs, 1.4f * F, false))
            open = false;

        ImGui::SetItemTooltip("Close");

        x -= 0.7f * F;
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);

        for (int t = tabs - 1; t >= 0; t--)
        {
            x -= bs;
            ImGui::SetCursorPos(ImVec2(x, by));
            ImGui::PushID(t);

            if (menu_icon_button("##tab", menu_tab_icon(t), bs, 1.45f * F, t == g_tab) && t != g_tab)
            {
                g_tab = t;
                g_focus_rows = true;
            }

            ImGui::SetItemTooltip("%s", settings_tab(t));
            ImGui::PopID();

            if (t == g_tab)
            {
                float u = floorf(0.16f * F > 2.0f ? 0.16f * F : 2.0f);
                dl->AddRectFilled(ImVec2(x + 0.35f * F, hh - u), ImVec2(x + bs - 0.35f * F, hh), ui_col(COL_ACCENT), u * 0.5f);
            }

            x -= 0.1f * F;
        }

        ImGui::PopItemFlag();

        float title_x = 0.9f * F;
        float title_w = x - 0.6f * F - title_x;
        char buf[128];
        float sub_size = 0.78f * F, tab_size = 1.35f * F;
        ImVec2 ss = ui_text_size(sub_size, "A"), bsz = ui_text_size(tab_size, "A");
        float ty = (hh - ss.y - bsz.y) * 0.5f;
        ui_text(dl, sub_size, ImVec2(title_x, ty), ui_col(COL_DIM), ui_fit(sub_size, "DirectCRT settings", title_w, buf, sizeof(buf)));
        ui_text(dl, tab_size, ImVec2(title_x, ty + ss.y), ui_col(COL_TEXT), ui_fit(tab_size, settings_tab(g_tab) ? settings_tab(g_tab) : "", title_w, buf, sizeof(buf)));

        /* The rows */
        float fh = floorf(2.4f * F);
        ImGui::SetCursorPos(ImVec2(0.0f, hh));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.55f * F, 0.5f * F));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.5f * F, 0.1f * F));
        ImGui::PushID(g_tab);

        if (g_focus_rows && !popup)
        {
            ImGui::SetNextWindowFocus();
            g_focus_rows = false;
        }

        if (ImGui::BeginChild("##rows", ImVec2(pw, ch - hh - fh), ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            menu_rows(g_tab, F);
            ImGui::Dummy(ImVec2(0.0f, 0.3f * F));
        }

        ImGui::EndChild();
        ImGui::PopID();
        ImGui::PopStyleVar(2);

        /* The footer: the keys */
        float fy = ch - fh;
        ImGui::SetCursorPos(ImVec2(0.0f, fy));
        ImGui::Dummy(ImVec2(pw, fh));
        dl->AddRectFilled(ImVec2(0, fy), ImVec2(pw, ch), ui_col(COL_BAR));
        dl->AddLine(ImVec2(0, fy + 0.5f), ImVec2(pw, fy + 0.5f), ui_col(COL_RULE));

        static const char* hints[][2] = {
            { "\xe2\x86\x90\xe2\x86\x92", "Change page" },
            { "\xe2\x86\x91\xe2\x86\x93", "Navigate" },
            { "Enter", "Select" },
            { "Esc", "Close" },
        };

        float hx = pw - 0.9f * F;

        for (int i = (int)(sizeof(hints) / sizeof(hints[0])) - 1; i >= 0; i--)
        {
            hx = menu_hint(dl, hx, fy + fh * 0.5f, 0.9f * F, hints[i][0], hints[i][1], F);

            if (hx < 0.0f)
                break;
        }

        menu_dialog(width, ch, F);
    }

    ImGui::End();

    /* The hotkey being set lost its dialog */
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
        menu_draw(width, canvas_height, menu_font);
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
