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
#define MENU_GROUPS 16

static bool g_menu_drawn;      /* the settings OSD was in the last frame */
static bool g_focus_rows;      /* give the list the keyboard next */
static int g_focus_row = -1;   /* and this row of it */
static int g_open = -1;        /* the row open under its heading, and how many settings share it */
static int g_open_count;
static bool g_open_scroll;     /* scroll it into view when it's known how tall it is */
static float g_scroll_to = -1.0f; /* where the list is gliding to */

/* The groups of the list (settings_tab), as the last frame laid them out */
static int g_groups;
static float g_group_y[MENU_GROUPS]; /* where each starts in the list */
static int g_group_row[MENU_GROUPS]; /* its first row */
static int g_group;                  /* the one the header names */
static int g_group_pinned = -1;      /* the one jumped to, named while the list stays where it went */
static float g_pinned_scroll;

static int g_capture = -1;     /* the hotkey being set */
static int g_slider = -1;      /* the slider being dragged, and its value */
static int g_slider_value;
static int g_number = -1;      /* the number box being typed in, and its number */
static int g_number_value;
static int g_text = -1;        /* the text being edited, and its text */
static char g_text_value[MENU_TEXT];
static int g_custom = -1;      /* the setting whose "A number" or "Other" was picked */
static bool g_custom_focus;
static bool g_focus_body;      /* give the row just opened with the keyboard its control */
static bool g_key_down[256];   /* the keys ImGui saw pressed */

static void menu_capture(int setting)
{
    g_capture = setting;
    overlay_menu_set_capturing(setting >= 0);

    /* The keys go to the capture now: ImGui won't see the Enter that opened
     * it let go */
    if (setting >= 0)
    {
        ImGui::GetIO().ClearInputKeys();
        memset(g_key_down, 0, sizeof(g_key_down));
    }
}

/* Closes the open row; focus = give its heading the keyboard */
static void menu_close_row(bool focus)
{
    if (g_capture >= 0)
        menu_capture(-1);

    if (focus && g_open >= 0)
        g_focus_row = g_open;

    g_open = -1;
    g_focus_body = false;
    g_custom = -1;
    g_slider = -1;
    g_number = -1;
    g_text = -1;
}

static void menu_reset(void)
{
    menu_close_row(false);
    g_focus_row = -1;
    g_scroll_to = -1.0f;
    g_group_pinned = -1;
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
    char line[1024], desc[1024];
    bool has_desc = menu_description(setting, desc, sizeof(desc));
    bool open = false;

    /* The help the row doesn't already show */
    while (menu_general_line(s, &cursor, line, sizeof(line), true))
    {
        if (has_desc && !strcmp(line, desc))
            continue;

        if (!open)
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(30.0f * F);
            open = true;
        }

        ImGui::TextUnformatted(line);
    }

    if (open)
    {
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/* ------------------------------------------------------------ the motion */

/* The frame's time step, for the animations: a long frame doesn't skip them */
static float ui_dt(void)
{
    float dt = ImGui::GetIO().DeltaTime;
    return dt < 0.0f ? 0.0f : dt > 0.1f ? 0.1f : dt;
}

/* x moved toward target by a part of the way that depends on the time, not
 * the frame rate; within snap of it, it's there */
static float ui_approach(float x, float target, float speed, float snap)
{
    x += (target - x) * (1.0f - expf(-ui_dt() * speed));
    return fabsf(target - x) < snap ? target : x;
}

/* A rectangle gliding to target: the highlight of a row, the pill of a
 * choice. It's kept relative to origin (where the window's content starts),
 * so it doesn't glide when the window scrolls. snap = jump there */
static ImRect ui_slide(ImGuiID id, const ImRect& target, ImVec2 origin, bool snap)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    float to[4] = { target.Min.x - origin.x, target.Min.y - origin.y, target.Max.x - origin.x, target.Max.y - origin.y };
    float v[4];
    bool known = st->GetBool(id, false) && !snap;

    for (int i = 0; i < 4; i++)
    {
        ImGuiID key = ImHashData(&i, sizeof(i), id);
        v[i] = known ? ui_approach(st->GetFloat(key, to[i]), to[i], 18.0f, 0.25f) : to[i];
        st->SetFloat(key, v[i]);
    }

    st->SetBool(id, true);
    return ImRect(v[0] + origin.x, v[1] + origin.y, v[2] + origin.x, v[3] + origin.y);
}

/* A chevron pointing right, turned by angle (radians) clockwise */
static void ui_chevron(ImDrawList* dl, ImVec2 c, float size, float angle, ImU32 col)
{
    float ca = cosf(angle), sa = sinf(angle);
    ImVec2 pts[3] = { ImVec2(-0.17f, -0.34f), ImVec2(0.17f, 0.0f), ImVec2(-0.17f, 0.34f) };

    for (int i = 0; i < 3; i++)
    {
        float x = pts[i].x * size, y = pts[i].y * size;
        pts[i] = ImVec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca);
    }

    float thick = size * 0.12f > 1.5f ? size * 0.12f : 1.5f;
    dl->AddPolyline(pts, 3, col, thick);
}

/* The keyboard is in use: its cursor shows, and the mouse hasn't moved since */
static bool ui_keyboard(void)
{
    ImGuiContext& g = *GImGui;
    return g.NavCursorVisible && g.NavHighlightItemUnderNav;
}

/* The keyboard goes to this item when its row was just opened with the
 * keyboard; called straight after its ItemAdd */
static void menu_take_focus(ImGuiID id)
{
    if (!g_focus_body)
        return;

    ImGuiContext& g = *GImGui;
    ImGui::SetFocusID(id, g.CurrentWindow);
    g.NavCursorVisible = true;
    g.NavHighlightItemUnderNav = true;
    g_focus_body = false;
}

/* Choices side by side, the one picked on a pill that glides to the next.
 * Returns the one clicked, or -1 */
static int ui_segmented(const char* str_id, const char* const* labels, int n, int selected, float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems || n <= 0 || n > 16)
        return -1;

    ImGuiContext& g = *GImGui;
    ImDrawList* dl = window->DrawList;
    ImGui::PushID(str_id);

    float h = floorf(2.1f * F), w = ImGui::GetContentRegionAvail().x;
    float inset = floorf(0.2f * F > 2.0f ? 0.2f * F : 2.0f), r = 0.45f * F;
    float label_size = 0.98f * F;
    ImVec2 pos = window->DC.CursorPos;
    ImRect bar(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bar);
    dl->AddRectFilled(bar.Min, bar.Max, ui_col(COL_PILL), r);

    ImRect segs[16];
    int picked = -1;

    for (int i = 0; i < n; i++)
    {
        ImRect seg(ImVec2(floorf(pos.x + w * i / n), pos.y), ImVec2(floorf(pos.x + w * (i + 1) / n), pos.y + h));
        segs[i] = ImRect(seg.Min + ImVec2(inset, inset), seg.Max - ImVec2(inset, inset));

        ImGuiID id = window->GetID(i);

        bool visible = ImGui::ItemAdd(seg, id);

        if (i == (selected >= 0 ? selected : 0))
            menu_take_focus(id);

        if (!visible)
            continue;

        bool hovered, held;

        if (ImGui::ButtonBehavior(seg, id, &hovered, &held))
            picked = i;

        if (i != selected && (hovered || (g.NavId == id && ui_keyboard())))
            dl->AddRectFilled(segs[i].Min, segs[i].Max, ui_col(held ? COL_ROW_ACTIVE : COL_PILL_HOVER), r - inset);

        ImGui::RenderNavCursor(seg, id, ImGuiNavRenderCursorFlags_None, r);

        if (i == selected)
            ImGui::SetItemDefaultFocus();
    }

    ImGuiID pill_id = window->GetID("##pill");
    ImRect pill;
    bool has_pill = selected >= 0 && selected < n;

    if (has_pill)
    {
        pill = ui_slide(pill_id, segs[selected], window->DC.CursorStartPos, false);
        dl->AddRectFilled(pill.Min, pill.Max, ui_col(COL_ACCENT), r - inset);
    }
    else
    {
        ImGui::GetStateStorage()->SetBool(pill_id, false); /* it appears where it's next put */
    }

    /* The text white as far as the pill is under it */
    static const ImVec4 off(0.80f, 0.81f, 0.83f, 1.0f);
    float vh = ui_text_size(label_size, "A").y;

    for (int i = 0; i < n; i++)
    {
        float k = 0.0f;

        if (has_pill)
        {
            float o = ImMin(pill.Max.x, segs[i].Max.x) - ImMax(pill.Min.x, segs[i].Min.x);
            k = ImClamp(o / (segs[i].GetWidth() > 1.0f ? segs[i].GetWidth() : 1.0f), 0.0f, 1.0f);
        }

        ImVec4 c(off.x + (1.0f - off.x) * k, off.y + (1.0f - off.y) * k, off.z + (1.0f - off.z) * k, 1.0f);
        char buf[MENU_TEXT];
        const char* text = ui_fit(label_size, labels[i], segs[i].GetWidth() - 0.5f * F, buf, sizeof(buf));
        float tw = ui_text_size(label_size, text).x;
        ui_text(dl, label_size, ImVec2((segs[i].Min.x + segs[i].Max.x - tw) * 0.5f, pos.y + (h - vh) * 0.5f), ui_col(c), text);
    }

    ImGui::PopID();
    return picked;
}

/* -------------------------------------------------- the settings OSD's rows */

/* A row as it was laid out, for the highlight drawn under it */
struct MENU_ROW
{
    ImGuiID id;
    ImRect bb;
    bool hovered, held;
};

/* Opens a row (settings first to first + count - 1) under its heading, or
 * closes it when it's open */
static void menu_open_row(int first, int count)
{
    if (g_open == first)
    {
        menu_close_row(false);
        return;
    }

    menu_close_row(false);

    const SETTING* s = settings_get(first);
    g_open = first;
    g_open_count = count;
    g_open_scroll = true;
    g_focus_body = ui_keyboard() && s->kind != SETTING_KEY && !(s->kind == SETTING_STRING && !s->choices);

    if (s->kind == SETTING_KEY)
        menu_capture(first);

    /* Text of its own only: it's typed in straight away */
    if (s->kind == SETTING_STRING && !s->choices)
    {
        g_custom = first;
        g_custom_focus = true;
    }
}

/* A row: its icon, its name and what it is, and its value (a switch, or what
 * opens it) on the right; settings first to first + count - 1. open = how far
 * it's open, which turns its chevron */
static void menu_row(int first, int count, float open, float F, MENU_ROW* out)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const SETTING* s = settings_get(first);
    ImDrawList* dl = window->DrawList;
    ImGuiID id = window->GetID("##row");

    out->id = id;
    out->hovered = out->held = false;

    float pad_x = 0.7f * F, pad_y = 0.55f * F;
    float icon_w = 1.9f * F;
    float title_size = 1.12f * F, desc_size = 0.88f * F, value_size = 1.0f * F;

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
    out->bb = bb;
    ImGui::ItemSize(bb);

    if (!ImGui::ItemAdd(bb, id))
        return;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    ImGuiContext& g = *GImGui;
    bool lit = hovered || (g.NavId == id && ui_keyboard());

    out->hovered = hovered;
    out->held = held;

    float cy = pos.y + h * 0.5f;
    bool changed = false;

    for (int m = first; m < first + count; m++)
        changed = changed || menu_changed(m);

    /* A dot: not the default */
    if (changed)
        dl->AddCircleFilled(ImVec2(pos.x + pad_x * 0.5f, cy), 0.16f * F, ui_col(COL_ACCENT), 12);

    ui_icon(dl, menu_setting_icon(s->key), ImVec2(pos.x + pad_x + icon_w * 0.4f, cy), 1.35f * F,
        ui_col(open > 0.5f ? COL_ACCENT_HI : COL_HEADING));

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
        ui_switch(dl, ImVec2(vx, cy - sh * 0.5f), value_w, sh, on != 0, lit);
    }
    else
    {
        float ph = 2.0f * F;
        ImVec2 a(vx, cy - ph * 0.5f), b(vx + value_w, cy + ph * 0.5f);
        dl->AddRectFilled(a, b, ui_col(lit ? COL_PILL_HOVER : COL_PILL), 0.45f * F);

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

        float e = open * open * (3.0f - 2.0f * open);
        ui_chevron(dl, ImVec2(b.x - chevron_w * 0.5f, cy), 1.0f * F, e * IM_PI * 0.5f, ui_col(open > 0.5f ? COL_TEXT : COL_DIM));
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
            menu_open_row(first, count);
        }
    }
}

/* A heading between a group's rows; its help in a tooltip. first = straight
 * under the group's heading */
static void menu_heading(const SETTING* s, bool first, float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return;

    float size = 0.95f * F;
    const char* text = s->label ? s->label : "";
    ImVec2 sz = ui_text_size(size, text);
    float above = first ? 0.2f * F : 0.9f * F;

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

/* A group's heading: its icon and its name. Returns where it starts, in the
 * list's own coordinates */
static float menu_group_heading(int tab, float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    float size = 1.25f * F;
    const char* text = settings_tab(tab);
    ImVec2 sz = ui_text_size(size, text);
    bool top = window->DC.CursorPos.y <= window->DC.CursorStartPos.y + 1.0f;
    float above = top ? 0.3f * F : 1.6f * F;

    ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + above);
    float w = ImGui::GetContentRegionAvail().x;
    ImRect bb(window->DC.CursorPos, ImVec2(pos.x + w, pos.y + sz.y + 0.2f * F));
    ImGui::ItemSize(bb);

    if (ImGui::ItemAdd(bb, 0))
    {
        ImDrawList* dl = window->DrawList;
        float ih = 1.45f * F;
        ui_icon(dl, menu_tab_icon(tab), ImVec2(pos.x + 0.7f * F + ih * 0.5f, pos.y + sz.y * 0.5f), ih, ui_col(COL_ACCENT_HI));
        ui_text(dl, size, ImVec2(pos.x + 0.7f * F + ih + 0.55f * F, pos.y), ui_col(COL_TEXT), text);
    }

    float y = pos.y - 0.5f * F - window->DC.CursorStartPos.y;
    return y > 0.0f ? y : 0.0f;
}

/* -------------------------------------------- what opens under a row */

#define MENU_OTHER -2 /* a value of its own: "A number" or "Other" */

/* Choices one under another, with their help; the one picked on a pill that
 * glides to the next. The last is "A number" or "Other" when own. Returns the
 * one clicked, or -1 */
static int menu_choice_list(const SETTING* s, const char* const* labels, int n, bool own, int selected, float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return -1;

    ImGuiContext& g = *GImGui;
    ImDrawList* dl = window->DrawList;
    float pad_y = 0.38f * F, check_w = 1.8f * F;
    float label_size = 1.0f * F, help_size = 0.86f * F;
    float rounding = 0.4f * F;
    float w = ImGui::GetContentRegionAvail().x;
    int picked = -1;
    ImRect selected_bb;
    bool has_selected = false;
    float label_h = ui_text_size(label_size, "A").y;

    for (int i = 0; i < n; i++)
    {
        char v[MENU_TEXT], l[MENU_TEXT], h[1024];
        bool has_help;

        if (own && i == n - 1)
        {
            has_help = settings_choice_help(s, NULL, h, sizeof(h)) != 0;
        }
        else
        {
            settings_choice(s, i, v, sizeof(v), l, sizeof(l));
            has_help = settings_choice_help(s, v, h, sizeof(h)) != 0;
        }

        const char* help = has_help && *h && _stricmp(h, labels[i]) ? h : NULL;

        ImGui::PushID(i == n - 1 && own ? MENU_OTHER : i);
        ImGuiID id = window->GetID("##choice");
        ImGui::PopID();

        ImVec2 pos = window->DC.CursorPos;
        float text_x = pos.x + check_w;
        float text_w = pos.x + w - 0.6f * F - text_x;
        ImVec2 label_sz = ui_text_size(label_size, labels[i], text_w);
        ImVec2 help_sz = help ? ui_text_size(help_size, help, text_w) : ImVec2(0, 0);
        float gap = help ? 0.06f * F : 0.0f;
        float rh = label_sz.y + gap + help_sz.y + 2 * pad_y;

        if (rh < 1.9f * F)
            rh = 1.9f * F;

        ImRect bb(pos, ImVec2(pos.x + w, pos.y + rh));
        ImGui::ItemSize(bb);

        if (i == selected)
        {
            selected_bb = bb;
            has_selected = true;
        }

        bool visible = ImGui::ItemAdd(bb, id);

        if (i == (selected >= 0 ? selected : 0))
            menu_take_focus(id);

        if (!visible)
            continue;

        bool hovered, held;

        if (ImGui::ButtonBehavior(bb, id, &hovered, &held))
            picked = i;

        if (i != selected && (hovered || (g.NavId == id && ui_keyboard())))
        {
            dl->ChannelsSetCurrent(0);
            dl->AddRectFilled(bb.Min, bb.Max, ui_col(held ? COL_ROW_ACTIVE : COL_ROW_HOVER), rounding);
            dl->ChannelsSetCurrent(1);
        }

        ImGui::RenderNavCursor(bb, id, ImGuiNavRenderCursorFlags_None, rounding);

        float content_h = label_sz.y + gap + help_sz.y;
        float ty = pos.y + (rh - content_h) * 0.5f;
        ui_text(dl, label_size, ImVec2(text_x, ty), ui_col(i == selected ? COL_ACCENT_HI : COL_TEXT), labels[i], text_w);

        if (help)
            ui_text(dl, help_size, ImVec2(text_x, ty + label_sz.y + gap), ui_col(COL_DIM), help, text_w);

        if (i == selected)
            ImGui::SetItemDefaultFocus();
    }

    /* The pill, under the choices, and its tick over them */
    ImGuiID pill_id = window->GetID("##pill");

    if (has_selected)
    {
        ImRect p = ui_slide(pill_id, selected_bb, window->DC.CursorStartPos, false);
        float bar = floorf(0.2f * F > 2.0f ? 0.2f * F : 2.0f);

        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(p.Min, p.Max, ui_col(COL_ACCENT, 0.20f), rounding);
        dl->AddRectFilled(p.Min, ImVec2(p.Min.x + bar, p.Max.y), ui_col(COL_ACCENT), rounding, ImDrawFlags_RoundLeft);
        dl->ChannelsSetCurrent(1);

        float ty = p.Min.y + pad_y + label_h * 0.5f;
        ui_icon(dl, ICON_CHECK, ImVec2(p.Min.x + check_w * 0.5f + bar * 0.5f, ty), 1.25f * F, ui_col(COL_ACCENT_HI));
    }
    else
    {
        ImGui::GetStateStorage()->SetBool(pill_id, false);
    }

    return picked;
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
        float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(tint->x * 0.5f + 0.5f, tint->y * 0.5f + 0.5f, tint->z * 0.5f + 0.5f, 1.0f), "%s", tint_name);
        ImGui::SameLine(x0 + 3.6f * F);
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

    bool slid = ImGui::SliderInt("##int", &v, s->min, s->max, "", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    menu_take_focus(ImGui::GetItemID());

    if (slid)
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

/* A text field, the width of the row. Returns true when Enter is pressed */
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

/* Text in the help's grey, wrapped, a little smaller */
static void menu_note(const char* text, float F, const ImVec4& col = COL_DIM)
{
    ImGui::PushFont(NULL, 0.9f * F);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

/* A box to type a number in, written when it's left. Returns true and the
 * number then */
static bool ui_int_field(const char* str_id, int v, int lo, int hi, float w, int* out)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGui::PushID(str_id);
    ImGuiID edited = ImGui::GetID("##edited"), typed = ImGui::GetID("##typed");
    bool commit = false;
    int n = v;

    ImGui::SetNextItemWidth(w);

    if (ImGui::InputInt("##n", &n, 0, 0))
    {
        st->SetBool(edited, true);
        st->SetInt(typed, n);
    }

    if (ImGui::IsItemDeactivated() && st->GetBool(edited))
    {
        st->SetBool(edited, false);

        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            n = st->GetInt(typed, v);
            *out = n < lo ? lo : n > hi ? hi : n;
            commit = true;
        }
    }

    ImGui::PopID();
    return commit;
}

/* A label, a slider and a box to type the number in. Returns true and the
 * number when the slider is let go or a number is typed in */
static bool ui_slider_box(const char* str_id, const char* label, int v, int lo, int hi, float F, int* out)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID(str_id);
    ImGuiID dragging = ImGui::GetID("##dragging"), dragged = ImGui::GetID("##dragged");
    bool commit = false;
    int cur = st->GetBool(dragging) ? st->GetInt(dragged, v) : v;

    if (label)
    {
        float x0 = ImGui::GetCursorPosX();
        float lw = ImGui::CalcTextSize(label).x + style.ItemSpacing.x * 2;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(x0 + (lw > 7.0f * F ? lw : 7.0f * F));
    }

    float box = ImGui::CalcTextSize("00000").x + style.FramePadding.x * 2;
    float slider = ImGui::GetContentRegionAvail().x - box - style.ItemSpacing.x;

    if (slider < ImGui::GetFrameHeight())
        slider = ImGui::GetFrameHeight();

    ImGui::SetNextItemWidth(slider);

    if (ImGui::SliderInt("##slider", &cur, lo, hi, "", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
    {
        st->SetBool(dragging, true);
        st->SetInt(dragged, cur);
    }

    if (ImGui::IsItemDeactivated() && st->GetBool(dragging))
    {
        st->SetBool(dragging, false);
        *out = st->GetInt(dragged, v);
        commit = true;
    }

    ImGui::SameLine();

    if (ui_int_field("##box", cur, lo, hi, box, out))
        commit = true;

    ImGui::PopID();
    return commit;
}

/* The desktop in pixels */
static void menu_desktop(int* w, int* h)
{
    DEVMODEA dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);

    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsWidth && dm.dmPelsHeight)
    {
        *w = (int)dm.dmPelsWidth;
        *h = (int)dm.dmPelsHeight;
    }
    else
    {
        *w = GetSystemMetrics(SM_CXSCREEN);
        *h = GetSystemMetrics(SM_CYSCREEN);
    }
}

enum
{
    VIEWPORT_FIT,
    VIEWPORT_SIZE,
    VIEWPORT_PIXELS,
    VIEWPORT_DESKTOP,
    VIEWPORT_RELATIVE,
};

/* A viewport's kind, and its numbers; what crt_auto.c doesn't take is "fit" */
static int menu_viewport_mode(const char* v, int* a, int* b)
{
    char rest[8] = "", sep = 0;

    if (!_strnicmp(v, "relative", 8) && sscanf(v + 8, " %d%% %d%%", a, b) == 2)
        return VIEWPORT_RELATIVE;

    int n = sscanf(v, "%d%c%d%7s", a, &sep, b, rest);

    if (n >= 3 && (sep == 'x' || sep == 'X' || sep == ','))
    {
        if (n == 3)
            return VIEWPORT_SIZE;

        if (!_stricmp(rest, "px"))
            return VIEWPORT_PIXELS;
    }

    if (sscanf(v, "%d", a) == 1 && strchr(v, '%'))
        return VIEWPORT_DESKTOP;

    return VIEWPORT_FIT;
}

static void menu_viewport_value(int mode, int a, int b, char* buf, size_t size)
{
    switch (mode)
    {
    case VIEWPORT_SIZE: _snprintf(buf, size - 1, "%dx%d", a, b); break;
    case VIEWPORT_PIXELS: _snprintf(buf, size - 1, "%dx%dpx", a, b); break;
    case VIEWPORT_DESKTOP: _snprintf(buf, size - 1, "%d%%", a); break;
    case VIEWPORT_RELATIVE: _snprintf(buf, size - 1, "relative %d%% %d%%", a, b); break;
    default: _snprintf(buf, size - 1, "fit"); break;
    }

    buf[size - 1] = 0;
}

/* The viewport, each of its kinds with its own controls: crt_auto.c's
 * formats without typing them */
static void menu_viewport(int setting, const SETTING* s, float F)
{
    /* Each kind's numbers, kept while another kind is tried */
    static int nums[5][2] = { { 0, 0 }, { 960, 720 }, { 1440, 1080 }, { 80, 0 }, { 100, 100 } };
    static bool linked = true;
    static const char* modes[] = { "Fit", "Size", "Pixels", "Desktop", "Relative" };
    static const char* formats[] = { "fit", "WxH", "WxHpx", "N%", "relative H% V%" };

    char value[MENU_TEXT], text[1024];
    strncpy(value, overlay_menu_value(setting, NULL), sizeof(value) - 1);
    value[sizeof(value) - 1] = 0;

    int a = 0, b = 0;
    int mode = menu_viewport_mode(value, &a, &b);

    if (mode != VIEWPORT_FIT)
    {
        nums[mode][0] = a;
        nums[mode][1] = b;
    }

    if (mode == VIEWPORT_RELATIVE && a != b)
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
        menu_note(text, F);

    int dw, dh;
    menu_desktop(&dw, &dh);
    float scale = overlay_dpi_scale();

    if (scale <= 0.0f)
        scale = 1.0f;

    char line[256];

    if (mode == VIEWPORT_SIZE || mode == VIEWPORT_PIXELS)
    {
        bool px = mode == VIEWPORT_PIXELS;
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

            if (menu_button(line, bw, presets[i][0] == a && presets[i][1] == b))
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
        menu_note(line, F);

        if (a > max_w || b > max_h)
            menu_note("That's larger than the desktop: the window or screen is used, as with Fit", F, COL_ACCENT_HI);
    }
    else if (mode == VIEWPORT_DESKTOP)
    {
        int n;

        if (ui_slider_box("##desktop", "Percentage", a, 1, 100, F, &n))
        {
            write_mode = mode;
            wa = n;
        }

        _snprintf(line, sizeof(line) - 1, "%d%% of the desktop is %d\xc3\x97%d pixels", a, (int)(dw * a / 100.0f), (int)(dh * a / 100.0f));
        line[sizeof(line) - 1] = 0;
        menu_note(line, F);
    }
    else if (mode == VIEWPORT_RELATIVE)
    {
        int n;

        if (ui_slider_box("##h", "Horizontal", a, 20, 300, F, &n))
        {
            write_mode = mode;
            wa = n;
            wb = linked ? n : b;
        }

        if (ui_slider_box("##v", "Vertical", b, 20, 300, F, &n))
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
            menu_note("The picture keeps its shape inside this area. Aspect ratio: Stretch fills it", F);

            if (menu_button("Stretch the picture", 0.0f, false))
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

/* What opens under a row: the rest of its help, its control, its default and Reset */
static void menu_editor(int first, int count, float F)
{
    const SETTING* s = settings_get(first);
    const SETTING* help = menu_help_of(first);
    const char* cursor = help && help->help ? help->help : "";
    const ImGuiStyle& style = ImGui::GetStyle();
    bool listed = s->choices && s->kind != SETTING_BOOL && s->kind != SETTING_KEY && count == 1;
    bool viewport = s->key && !strcmp(s->key, "viewport");
    char text[1024], desc[1024];
    bool has_desc = menu_description(first, desc, sizeof(desc));

    /* The help the row doesn't show; the lines about the values are with them */
    ImGui::PushFont(NULL, 0.9f * F);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::PushTextWrapPos(0.0f);

    while (menu_general_line(help, &cursor, text, sizeof(text), !listed && !viewport))
    {
        if (!has_desc || strcmp(text, desc))
            ImGui::TextUnformatted(text);
    }

    ImGui::PopStyleColor();

    if (s->apply == SETTING_RESTART)
        ImGui::TextColored(COL_ACCENT_HI, "Takes effect when the game starts again");

    ImGui::PopTextWrapPos();
    ImGui::PopFont();

    char value[MENU_TEXT];
    strncpy(value, overlay_menu_value(first, NULL), sizeof(value) - 1);
    value[sizeof(value) - 1] = 0;

    bool number = s->kind == SETTING_INT;
    bool editor = (s->kind == SETTING_INT || s->kind == SETTING_STRING) && (!s->choices || count > 1);

    if (viewport)
    {
        menu_viewport(first, s, F);
        editor = false;
    }
    else if (listed)
    {
        /* The choices, and "A number" or "Other": side by side when they're
         * few and short, the picked one's help under them; else a list */
        int current = menu_find_choice(s, value, NULL, 0);

        if (current < 0 && s->kind == SETTING_CHOICE)
            current = menu_find_choice(s, s->value, NULL, 0);

        bool own = s->kind != SETTING_CHOICE;
        bool on_own = own && (current < 0 || g_custom == first);

        static char names[17][64];
        const char* labels[17];
        int n = 0;
        char v[MENU_TEXT], l[MENU_TEXT];

        for (int i = 0; n < 16 && settings_choice(s, i, v, sizeof(v), l, sizeof(l)); i++)
        {
            strncpy(names[n], l, sizeof(names[n]) - 1);
            names[n][sizeof(names[n]) - 1] = 0;
            labels[n] = names[n];
            n++;
        }

        if (own)
            labels[n++] = number ? "A number" : "Other";

        int selected = on_own ? n - 1 : current;
        float seg_w = ImGui::GetContentRegionAvail().x / (n ? n : 1);
        bool side = n <= 5;

        for (int i = 0; side && i < n; i++)
            side = ui_text_size(0.98f * F, labels[i]).x + 1.2f * F <= seg_w;

        int picked;

        if (side)
        {
            picked = ui_segmented("##choices", labels, n, selected, F);

            char h[1024];
            int has_help = 0;

            if (own && selected == n - 1)
                has_help = settings_choice_help(s, NULL, h, sizeof(h));
            else if (selected >= 0 && settings_choice(s, selected, v, sizeof(v), l, sizeof(l)))
                has_help = settings_choice_help(s, v, h, sizeof(h));

            if (has_help && _stricmp(h, labels[selected]))
                menu_note(h, F);
        }
        else
        {
            picked = menu_choice_list(s, labels, n, own, selected, F);
        }

        if (picked >= 0 && picked != selected)
        {
            if (own && picked == n - 1)
            {
                g_custom = first;
                g_custom_focus = true;
            }
            else
            {
                g_custom = -1;
                menu_write_choice(first, s, picked, value);
            }
        }

        editor = own && (on_own || g_custom == first);
    }

    if (editor)
    {
        if (number)
        {
            for (int m = first; m < first + count; m++)
                menu_number(m, settings_get(m), F);
        }
        else if (menu_text_field(first))
        {
            menu_close_row(true);
        }
    }

    /* A hotkey: what's pressed next */
    if (s->kind == SETTING_KEY)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool capturing = g_capture == first;
        float bh = 2.6f * F;
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, bh));
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        dl->AddRectFilled(a, b, ui_col(COL_PILL), 0.45f * F);

        if (capturing)
        {
            float pulse = 0.55f + 0.45f * sinf((float)ImGui::GetTime() * 5.0f);
            dl->AddRect(a, b, ui_col(COL_ACCENT, pulse), 0.45f * F, 0.12f * F > 1.0f ? 0.12f * F : 1.0f);
            strcpy(text, "Press a key   (Esc cancels)");
        }
        else
        {
            menu_key_name((int)strtol(value, NULL, 0), text, sizeof(text), true);
        }

        float size = 1.1f * F;
        ImVec2 sz = ui_text_size(size, text);
        ui_text(dl, size, ImVec2((a.x + b.x - sz.x) * 0.5f, (a.y + b.y - sz.y) * 0.5f), ui_col(capturing ? COL_TEXT : COL_DIM), text);

        /* A key was pressed, or Esc */
        if (!capturing && g_open == first)
            menu_close_row(ui_keyboard());
    }

    /* The default, and Reset (and None for a hotkey) */
    bool changed = false;
    char defaults[MENU_TEXT * 2] = "";

    for (int m = first; m < first + count; m++)
    {
        changed = changed || menu_changed(m);
        menu_append_value(defaults, sizeof(defaults), settings_get(m), settings_get(m)->value);
    }

    bool key = s->kind == SETTING_KEY;
    float bw = ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 4;
    float buttons_w = key ? bw * 2 + style.ItemSpacing.x : bw;
    float x0 = ImGui::GetCursorPosX(), iw = ImGui::GetContentRegionAvail().x;
    char line[MENU_TEXT * 2 + 16], cut[MENU_TEXT * 2 + 16];
    _snprintf(line, sizeof(line) - 1, "Default: %s", defaults);
    line[sizeof(line) - 1] = 0;

    ImGui::Dummy(ImVec2(0.0f, 0.05f * F));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::TextUnformatted(ui_fit(ImGui::GetFontSize(), line, iw - buttons_w - style.ItemSpacing.x * 2, cut, sizeof(cut)));
    ImGui::PopStyleColor();
    ImGui::SameLine(x0 + iw - buttons_w);

    if (key)
    {
        if (menu_button("None", bw, false))
        {
            overlay_menu_write(first, "0x00");
            menu_close_row(ui_keyboard());
        }

        ImGui::SameLine();
    }

    ImGui::BeginDisabled(!changed);

    if (menu_button("Reset", bw, false))
    {
        for (int m = first; m < first + count; m++)
        {
            if (menu_changed(m))
                overlay_menu_write(m, settings_get(m)->value);
        }

        g_slider = g_number = g_text = -1;
        g_custom = -1;

        if (key)
            menu_close_row(ui_keyboard());
    }

    ImGui::EndDisabled();
}

/* What opens under a row, sliding open and shut. Returns how far it's open
 * (0 to 1); bottom = where it ends */
static float menu_body(int first, int count, float F, float* bottom)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiStorage* st = &window->StateStorage;
    ImGuiID shown_id = window->GetID("##shown"), full_id = window->GetID("##full");
    bool open = g_open == first;
    float shown = st->GetFloat(shown_id, 0.0f);
    float full = st->GetFloat(full_id, 0.0f);

    *bottom = window->DC.CursorPos.y;

    if (!open && shown <= 0.0f)
        return 0.0f;

    /* Its height isn't known until it's laid out once: a pixel of it then */
    float h = open && full <= 0.0f ? 1.0f : ui_approach(shown, open ? full : 0.0f, 16.0f, 0.5f);
    st->SetFloat(shown_id, h);

    if (h <= 0.0f)
        return 0.0f;

    float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImFloor(0.7f * F), ImFloor(0.3f * F)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.5f * F, 0.45f * F));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.6f * F, 0.4f * F));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

    if (ImGui::BeginChild("##body", ImVec2(w, floorf(h > 1.0f ? h : 1.0f)), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding, flags))
    {
        ImGuiWindow* body = ImGui::GetCurrentWindow();
        ImDrawList* dl = body->DrawList;

        /* Channel 0 is under the rest: the pill of a list of choices */
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::Indent(1.9f * F);
        menu_editor(first, count, F);
        ImGui::Unindent(1.9f * F);
        dl->ChannelsMerge();

        float measured = floorf(body->DC.CursorMaxPos.y - body->DC.CursorStartPos.y + body->WindowPadding.y * 2.0f + 0.4f * F);

        if (measured > 0.0f)
            st->SetFloat(full_id, measured);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);

    *bottom = ImGui::GetItemRectMax().y;

    full = st->GetFloat(full_id, 0.0f);
    return full > 0.0f ? ImClamp(h / full, 0.0f, 1.0f) : 0.0f;
}

/* How far a row is open (as menu_body left it), in its ID scope */
static float menu_open_amount(void)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    float full = window->StateStorage.GetFloat(window->GetID("##full"), 0.0f);
    float shown = window->StateStorage.GetFloat(window->GetID("##shown"), 0.0f);
    return full > 0.0f ? ImClamp(shown / full, 0.0f, 1.0f) : 0.0f;
}

/* ------------------------------------------------ the settings OSD's list */

/* The list goes to a group: its heading at the top, or as far as it goes */
static void menu_go_to_group(int group, bool focus)
{
    if (group < 0 || group >= g_groups)
        return;

    g_scroll_to = g_group_y[group];
    g_group_pinned = group;
    g_pinned_scroll = -1.0f;

    if (focus && g_group_row[group] >= 0)
        g_focus_row = g_group_row[group];
}

/* Every setting, a group (settings_tab) after another. A setting marked
 * SETTING_JOINED shares the row of the one before */
static void menu_list(float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = window->DrawList;

    /* Gliding to a group, unless the wheel or the keys move the list */
    if (g_scroll_to >= 0.0f)
    {
        if (io.MouseWheel != 0.0f || ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
            ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            g_scroll_to = -1.0f;
        }
        else
        {
            float to = ImMin(g_scroll_to, ImGui::GetScrollMaxY());
            float y = ui_approach(ImGui::GetScrollY(), to, 14.0f, 1.0f);
            ImGui::SetScrollY(y);

            if (y == to)
                g_scroll_to = -1.0f;
        }
    }

    /* Channel 0 is under the rows: the highlight, and the open rows' cards */
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    ImVec2 origin = window->DC.CursorStartPos;
    ImRect target;
    bool has_target = false, target_held = false;
    struct { ImRect r; float a; } cards[4];
    int n_cards = 0;
    int nav_group = -1, open_group = -1;
    bool nav_on_row = false;

    g_groups = 0;

    for (int tab = 0; settings_tab(tab) && g_groups < MENU_GROUPS; tab++)
    {
        const char* name = settings_tab(tab);
        bool gated = false; /* a SETTING_GATE before is off */
        bool top = true;    /* straight under the group's heading */

        g_group_y[g_groups] = menu_group_heading(tab, F);
        g_group_row[g_groups] = -1;

        for (int i = 0; i < settings_count(); i++)
        {
            const SETTING* s = settings_get(i);

            if (!s->tab || strcmp(s->tab, name))
                continue;

            if (s->kind == SETTING_HEADING)
            {
                menu_heading(s, top, F);
                top = false;
                continue;
            }

            if (!s->key || (s->flags & SETTING_JOINED))
                continue;

            int count = 1;

            while (i + count < settings_count() && (settings_get(i + count)->flags & SETTING_JOINED))
                count++;

            top = false;

            if (g_group_row[g_groups] < 0)
                g_group_row[g_groups] = i;

            ImGui::PushID(i);
            ImGui::BeginDisabled(gated);

            MENU_ROW row;
            menu_row(i, count, menu_open_amount(), F, &row);

            if (g_focus_row == -2)
                g_focus_row = i; /* the first row, when the OSD opens */

            if (g_focus_row == i)
            {
                ImGui::SetFocusID(row.id, window);
                g.NavCursorVisible = true;
                g.NavHighlightItemUnderNav = true;
                g_focus_row = -1;
            }

            if (g.NavId == row.id)
            {
                nav_group = g_groups;
                nav_on_row = true;
            }

            /* The keyboard's row, or else the mouse's */
            bool nav_here = g.NavId == row.id && ui_keyboard();

            if (nav_here || (!ui_keyboard() && row.hovered))
            {
                target = row.bb;
                target_held = row.held && row.hovered;
                has_target = true;
            }

            float bottom;
            float amount = menu_body(i, count, F, &bottom);

            if (amount > 0.0f && n_cards < 4)
            {
                cards[n_cards].r = ImRect(row.bb.Min, ImVec2(row.bb.Max.x, bottom));
                cards[n_cards].a = amount;
                n_cards++;
            }

            if (g_open == i)
            {
                open_group = g_groups;

                /* Its whole height is known: scroll it into view */
                float full = window->StateStorage.GetFloat(window->GetID("##full"), 0.0f);

                if (g_open_scroll && full > 0.0f)
                {
                    float top_y = row.bb.Min.y - origin.y;
                    float end_y = row.bb.Max.y - origin.y + full;
                    float view = window->InnerRect.GetHeight();
                    float sy = g_scroll_to >= 0.0f ? g_scroll_to : ImGui::GetScrollY();

                    if (end_y > sy + view)
                    {
                        float to = end_y - view + 0.5f * F;
                        g_scroll_to = to < top_y - 0.5f * F ? to : top_y - 0.5f * F;
                    }

                    g_open_scroll = false;
                }
            }

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

        g_groups++;
    }

    ImGui::Dummy(ImVec2(0.0f, 0.6f * F));

    /* Under the rows */
    dl->ChannelsSetCurrent(0);
    float rounding = 0.4f * F;

    for (int i = 0; i < n_cards; i++)
        dl->AddRectFilled(cards[i].r.Min, cards[i].r.Max, ui_col(COL_DIALOG, cards[i].a), rounding);

    ImGuiStorage* st = &window->StateStorage;
    ImGuiID hl_id = window->GetID("##highlight"), hl_alpha = window->GetID("##highlight_alpha");
    float alpha = st->GetFloat(hl_alpha, 0.0f);

    if (has_target)
    {
        ImRect r = ui_slide(hl_id, target, origin, alpha < 0.05f);
        alpha = ui_approach(alpha, 1.0f, 20.0f, 0.02f);
        dl->AddRectFilled(r.Min, r.Max, ui_col(target_held ? COL_ROW_ACTIVE : COL_ROW_HOVER, alpha), rounding);
    }
    else if (alpha > 0.0f)
    {
        /* Fading where it was */
        ImRect r = ui_slide(hl_id, ImRect(0, 0, 0, 0), ImVec2(0, 0), false);
        (void)r;
        alpha = ui_approach(alpha, 0.0f, 20.0f, 0.02f);
    }

    st->SetFloat(hl_alpha, alpha);
    dl->ChannelsSetCurrent(1);
    dl->ChannelsMerge();

    /* The group the header names: the keyboard's, the one jumped to, or the
     * one at the top */
    float sy = ImGui::GetScrollY();

    if (g_group_pinned >= 0 && g_scroll_to < 0.0f)
    {
        if (g_pinned_scroll < 0.0f)
            g_pinned_scroll = sy;
        else if (fabsf(sy - g_pinned_scroll) > 2.0f)
            g_group_pinned = -1;
    }

    if (ui_keyboard() && nav_group >= 0)
    {
        g_group = nav_group;
    }
    else if (ui_keyboard() && open_group >= 0 && !nav_on_row && g.NavWindow == window)
    {
        g_group = open_group;
    }
    else if (g_group_pinned >= 0)
    {
        g_group = g_group_pinned;
    }
    else
    {
        g_group = 0;

        for (int i = 0; i < g_groups; i++)
        {
            if (g_group_y[i] <= sy + 1.5f * F)
                g_group = i;
        }
    }

    /* Left and Right go to the group before and after, from a row's heading */
    if (nav_on_row && g_capture < 0 && !ImGui::IsAnyItemActive() && g_groups > 0)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            menu_go_to_group((g_group + g_groups - 1) % g_groups, true);
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            menu_go_to_group((g_group + 1) % g_groups, true);
    }
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
        g_focus_row = -2;
    }

    bool open = true;

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
        ImGuiWindow* panel = ImGui::GetCurrentWindow();
        ImDrawList* dl = panel->DrawList;
        int groups = 0;

        while (settings_tab(groups))
            groups++;

        if (g_group >= groups)
            g_group = 0;

        /* Esc closes the open row, or the OSD; not while a text or a key is being typed */
        if (g_capture < 0 && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            if (g_open >= 0)
                menu_close_row(true);
            else
                open = false;
        }

        /* The keyboard goes to the list, not the panel around it */
        if (ImGui::IsWindowFocused())
            g_focus_rows = true;

        /* The header: the group's name, and the groups' icons to go to them */
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
        ImRect underline;

        for (int t = groups - 1; t >= 0; t--)
        {
            x -= bs;
            ImGui::SetCursorPos(ImVec2(x, by));
            ImGui::PushID(t);

            if (menu_icon_button("##group", menu_tab_icon(t), bs, 1.45f * F, t == g_group))
                menu_go_to_group(t, false);

            ImGui::SetItemTooltip("%s", settings_tab(t));
            ImGui::PopID();

            if (t == g_group)
            {
                float u = floorf(0.16f * F > 2.0f ? 0.16f * F : 2.0f);
                underline = ImRect(ImVec2(x + 0.35f * F, hh - u), ImVec2(x + bs - 0.35f * F, hh));
            }

            x -= 0.1f * F;
        }

        ImGui::PopItemFlag();

        /* The line under the group's icon glides to the next */
        if (groups > 0)
        {
            ImRect u = ui_slide(panel->GetID("##underline"), underline, panel->Pos, false);
            dl->AddRectFilled(u.Min, u.Max, ui_col(COL_ACCENT), u.GetHeight() * 0.5f);
        }

        float title_x = 0.9f * F;
        float title_w = x - 0.6f * F - title_x;
        char buf[128];
        float sub_size = 0.78f * F, group_size = 1.35f * F;
        ImVec2 ss = ui_text_size(sub_size, "A"), gs = ui_text_size(group_size, "A");
        float ty = (hh - ss.y - gs.y) * 0.5f;
        ui_text(dl, sub_size, ImVec2(title_x, ty), ui_col(COL_DIM), ui_fit(sub_size, "DirectCRT settings", title_w, buf, sizeof(buf)));
        ui_text(dl, group_size, ImVec2(title_x, ty + ss.y), ui_col(COL_TEXT),
            ui_fit(group_size, settings_tab(g_group) ? settings_tab(g_group) : "", title_w, buf, sizeof(buf)));

        /* The list */
        float fh = floorf(2.4f * F);
        ImGui::SetCursorPos(ImVec2(0.0f, hh));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.55f * F, 0.5f * F));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.5f * F, 0.1f * F));

        if (g_focus_rows)
        {
            ImGui::SetNextWindowFocus();
            g_focus_rows = false;
        }

        if (ImGui::BeginChild("##rows", ImVec2(pw, ch - hh - fh), ImGuiChildFlags_AlwaysUseWindowPadding))
            menu_list(F);

        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        /* The footer: the keys */
        float fy = ch - fh;
        ImGui::SetCursorPos(ImVec2(0.0f, fy));
        ImGui::Dummy(ImVec2(pw, fh));
        dl->AddRectFilled(ImVec2(0, fy), ImVec2(pw, ch), ui_col(COL_BAR));
        dl->AddLine(ImVec2(0, fy + 0.5f), ImVec2(pw, fy + 0.5f), ui_col(COL_RULE));

        const char* hints[4][2] = {
            { "\xe2\x86\x90\xe2\x86\x92", "Section" },
            { "\xe2\x86\x91\xe2\x86\x93", "Navigate" },
            { "Enter", g_open >= 0 ? "Select" : "Open" },
            { "Esc", g_open >= 0 ? "Back" : "Close" },
        };

        float hx = pw - 0.9f * F;

        for (int i = 3; i >= 0; i--)
        {
            hx = menu_hint(dl, hx, fy + fh * 0.5f, 0.9f * F, hints[i][0], hints[i][1], F);

            if (hx < 0.0f)
                break;
        }
    }

    ImGui::End();

    /* The hotkey being set lost its row */
    if (g_capture >= 0 && g_capture != g_open)
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
