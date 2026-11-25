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
#include "overlay_widgets.h"
#include "overlay_menu_ui.h"

extern "C" {
#include "dd.h"
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

ImFont* g_font;
ImFont* g_icons;
#define OVERLAY_FONT_SCALE 1.2f
static ImGuiStyle g_base_style;
static float g_style_scale;
static LARGE_INTEGER g_last_frame;

static OVERLAY_BACKEND g_backend;
static ID3D11Device* g_dx_device;  /* the device the backend was set up with */
static bool g_dx_failed;
static bool g_ogl_failed;
static bool g_menu_drawn; /* the settings OSD was in the last frame */

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
            /* Two columns, as wide as their text this frame: a table would
               take its width from the window, which takes its own from the
               table, and it would hardly shrink when the size goes down */
            float label_w = 0.0f, value_w = 0.0f, gap = 10.0f * sc;

            for (int i = 0; i < status.rows; i++)
            {
                label_w = ImMax(label_w, ImGui::CalcTextSize(status.row[i].label).x);
                value_w = ImMax(value_w, ImGui::CalcTextSize(status.row[i].value).x);
            }

            float x0 = ImGui::GetCursorPosX();

            for (int i = 0; i < status.rows; i++)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
                ImGui::TextUnformatted(status.row[i].label);
                ImGui::PopStyleColor();
                ImGui::SameLine(x0 + label_w + gap);
                ImGui::TextUnformatted(status.row[i].value);
            }

            float graph_width = ImMax(OVERLAY_HISTORY * sc, label_w + gap + value_w);

            ImGui::Dummy(ImVec2(0.0f, 2.0f * sc));
            overlay_draw_graph(status, graph_width, 44.0f * sc);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
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
    {
        overlay_get_status(&status, width, canvas_height);

        /* The settings OSD is on the left: the status OSD steps right for it */
        if (menu && status.corner == OVERLAY_TOP_LEFT)
            status.corner = OVERLAY_TOP_RIGHT;
        else if (menu && status.corner == OVERLAY_BOTTOM_LEFT)
            status.corner = OVERLAY_BOTTOM_RIGHT;
    }

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
        menu_draw(width, canvas_height, menu_font, !g_menu_drawn);
        ImGui::PopFont();
    }

    g_menu_drawn = menu;

    ImGui::Render();

    /* A change from the settings OSD is applied when it stands still */
    if (menu)
        overlay_menu_frame_drawn(g_ui_moving);

    g_ui_moving = false;
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
