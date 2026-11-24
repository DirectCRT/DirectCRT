/* The status OSD, drawn with Dear ImGui (third_party/imgui) over the finished
 * frame. There's no platform backend: the OSD takes no input, so the
 * display size and the time step are set here. The ImGui context lives as
 * long as the process; the renderer backend is set up the first time the
 * OSD is drawn and shut down with the renderer's resources. */

#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_opengl3.h"
#include "overlay.h"

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

/* One ImGui frame with the OSD in it; the renderer backend's NewFrame comes
 * first and its RenderDrawData after */
static void overlay_build_frame(int width, int height, int canvas_height)
{
    OVERLAY_STATUS status;
    overlay_get_status(&status, width, canvas_height);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);

    float dt = g_last_frame.QuadPart ? (float)((double)(now.QuadPart - g_last_frame.QuadPart) / freq.QuadPart) : 0.0f;
    io.DeltaTime = dt > 0.0001f ? dt : 1.0f / 60.0f;
    g_last_frame = now;

    if (status.scale != g_style_scale)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style = g_base_style;
        style.ScaleAllSizes(status.scale);
        g_style_scale = status.scale;
    }

    ImGui::NewFrame();
    ImGui::PushFont(g_font, status.font_size);

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
    if (!overlay_status_visible() || !device || !ctx || !rtv || width <= 0 || height <= 0)
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
    if (!overlay_status_visible() || width <= 0 || height <= 0 || canvas_height <= 0)
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
