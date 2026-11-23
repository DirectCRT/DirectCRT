#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include "fps_limiter.h"
#include "dd.h"
#include "ddsurface.h"
#include "render_d3d11.h"
#include "render_gdi.h"
#include "crt_auto.h"
#include "utils.h"
#include "blt.h"
#include "debug.h"
#include "hook.h"
#include "config.h"
#include "dllmain.h"
#include "d3d11_shaders/d3d11_shaders.h"

/* The passes, with crt-auto (as the OpenGL renderer does them):
 *
 *   convert:           game surface -> conv (RGBA8, game size)
 *   image adjustments: conv -> adj (RGBA8, crt.render_width x render_height)
 *   CRT shader:        adj -> back buffer, viewport = picture
 *
 * The image adjustments vertex shader negates y, so 'adj' has its rows in
 * the same order as the OpenGL framebuffer, and SV_Position is exactly
 * gl_FragCoord. The CRT shader draws the right way up and gets OpenGL's
 * gl_FragCoord and v_texCoord from SV_Position and the cbuffer in b1,
 * 'window_cb' (see window_pixel_shader in gen_d3d11_shaders.py).
 *
 * Without crt-auto: convert, then scale conv to the viewport. */

#ifdef _DEBUG
#define FAILEDX(stmt) d3d11_check_failed(stmt, #stmt)
static BOOL d3d11_check_failed(HRESULT hr, const char* stmt)
{
    if (FAILED(hr))
    {
        TRACE("Direct3D11 error %08x (%s)\n", (unsigned)hr, stmt);
        return TRUE;
    }

    return FALSE;
}
#else
#define FAILEDX(stmt) FAILED(stmt)
#endif

#define SAFE_RELEASE(p) do { if (p) { IUnknown_Release((IUnknown*)(p)); (p) = NULL; } } while (0)

/* Defined here so that neither toolchain needs dxguid */
static const GUID D3D11_IID_IDXGIDevice =
    { 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
static const GUID D3D11_IID_IDXGIDevice1 =
    { 0x77db970f, 0x6276, 0x48ba, { 0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c } };
static const GUID D3D11_IID_IDXGIFactory2 =
    { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
static const GUID D3D11_IID_IDXGIFactory5 =
    { 0x7632e1f5, 0xee65, 0x4dca, { 0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d } };
static const GUID D3D11_IID_ID3D11Texture2D =
    { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

typedef HRESULT(WINAPI* D3D11CREATEDEVICEPROC)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

typedef struct D3D11_TARGET
{
    ID3D11Texture2D* tex;
    ID3D11RenderTargetView* rtv;
    ID3D11ShaderResourceView* srv;
    int width;
    int height;
} D3D11_TARGET;

/* One translated DOSBox shader, with its uniforms */
typedef struct D3D11_PASS
{
    const D3D11_SHADER* shader;
    ID3D11VertexShader* vs;
    ID3D11PixelShader* ps;
    ID3D11Buffer* vs_cb;
    ID3D11Buffer* ps_cb;
    unsigned char vs_data[256];
    unsigned char ps_data[256];
} D3D11_PASS;

typedef struct D3D11RENDERER
{
    HMODULE hmodule;
    HWND hwnd;
    ID3D11Device* device;
    ID3D11DeviceContext* ctx;
    IDXGISwapChain1* swap_chain;
    BOOL tearing;
    UINT swap_chain_flags;

    /* Back buffer, the swap chain's size */
    ID3D11RenderTargetView* back_rtv;
    int back_width;
    int back_height;

    /* For the current mode, made by the render thread */
    ID3D11Buffer* quad_vb;
    ID3D11InputLayout* layout;
    ID3D11RasterizerState* raster;
    ID3D11SamplerState* point;
    ID3D11SamplerState* linear;
    ID3D11VertexShader* vs_quad;
    ID3D11PixelShader* ps_convert;
    ID3D11PixelShader* ps_plain;
    ID3D11Buffer* window_cb;
    float window_cb_data[8];
    ID3D11Texture2D* surface_tex;
    ID3D11ShaderResourceView* surface_srv;
    ID3D11Texture2D* palette_tex;
    ID3D11ShaderResourceView* palette_srv;
    D3D11_TARGET conv;
    D3D11_TARGET adj;

    BOOL crt_auto;
    D3D11_PASS adj_pass;
    D3D11_PASS crt_pass;
    CRT_AUTO_PARAMS crt_params;

#ifdef _DEBUG
    ID3D11Query* gpu_disjoint;
    ID3D11Query* gpu_start;
    ID3D11Query* gpu_end;
    BOOL gpu_time_pending;
    double gpu_time_ms;
    int gpu_time_frames;
#endif
} D3D11RENDERER;

static D3D11RENDERER g_d3d11;

static BOOL d3d11_create_device();
static BOOL d3d11_create_resources();
static void d3d11_release_resources();
static BOOL d3d11_update_back_buffer();
static void d3d11_render();

BOOL d3d11_is_available()
{
    if (!g_d3d11.hmodule)
        g_d3d11.hmodule = real_LoadLibraryA("d3d11.dll");

    return g_d3d11.hmodule && real_GetProcAddress(g_d3d11.hmodule, "D3D11CreateDevice");
}

BOOL d3d11_create()
{
    if (g_d3d11.hwnd == g_ddraw.hwnd && g_d3d11.device && g_d3d11.swap_chain)
        return TRUE;

    d3d11_release();

    return d3d11_create_device();
}

static BOOL d3d11_create_device()
{
    if (!d3d11_is_available())
        return FALSE;

    D3D11CREATEDEVICEPROC create_device =
        (D3D11CREATEDEVICEPROC)real_GetProcAddress(g_d3d11.hmodule, "D3D11CreateDevice");

    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL level = 0;
    HRESULT hr = E_FAIL;

#ifdef _DEBUG
    /* Needs the Graphics Tools optional feature */
    hr = create_device(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, levels,
        sizeof(levels) / sizeof(levels[0]), D3D11_SDK_VERSION, &g_d3d11.device, &level, &g_d3d11.ctx);
#endif

    if (FAILED(hr))
    {
        hr = create_device(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels,
            sizeof(levels) / sizeof(levels[0]), D3D11_SDK_VERSION, &g_d3d11.device, &level, &g_d3d11.ctx);
    }

    if (FAILED(hr))
    {
        /* No 11.1 runtime */
        hr = create_device(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels + 1,
            sizeof(levels) / sizeof(levels[0]) - 1, D3D11_SDK_VERSION, &g_d3d11.device, &level, &g_d3d11.ctx);
    }

    if (FAILED(hr))
    {
        TRACE("Direct3D11: D3D11CreateDevice failed %08x\n", (unsigned)hr);
        return FALSE;
    }

    IDXGIDevice* dxgi_device = NULL;
    IDXGIDevice1* dxgi_device1 = NULL;
    IDXGIAdapter* adapter = NULL;
    IDXGIFactory2* factory = NULL;
    IDXGIFactory5* factory5 = NULL;
    BOOL ok = FALSE;

    if (SUCCEEDED(ID3D11Device_QueryInterface(g_d3d11.device, &D3D11_IID_IDXGIDevice, (void**)&dxgi_device)) &&
        SUCCEEDED(IDXGIDevice_GetAdapter(dxgi_device, &adapter)) &&
        SUCCEEDED(IDXGIAdapter_GetParent(adapter, &D3D11_IID_IDXGIFactory2, (void**)&factory)))
    {
        DXGI_ADAPTER_DESC ad;

        if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &ad)))
        {
            TRACE("+--Direct3D11-------------------------------------\n");
            TRACE("| Adapter:             %ls\n", ad.Description);
            TRACE("| VendorId:            0x%x\n", ad.VendorId);
            TRACE("| DeviceId:            0x%x\n", ad.DeviceId);
            TRACE("| FeatureLevel:        %x.%x\n", (level >> 12) & 0xF, (level >> 8) & 0xF);
            TRACE("+------------------------------------------------\n");
        }

        /* Lets vsync=false present at once on a borderless fullscreen window */
        g_d3d11.tearing = FALSE;

        if (SUCCEEDED(IDXGIFactory2_QueryInterface(factory, &D3D11_IID_IDXGIFactory5, (void**)&factory5)))
        {
            BOOL allow = FALSE;

            if (SUCCEEDED(IDXGIFactory5_CheckFeatureSupport(
                factory5, DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))) && allow)
            {
                g_d3d11.tearing = TRUE;
            }

            IDXGIFactory5_Release(factory5);
        }

        g_d3d11.swap_chain_flags = g_d3d11.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        DXGI_SWAP_CHAIN_DESC1 desc;
        memset(&desc, 0, sizeof(desc));
        desc.Width = 0; /* the client area */
        desc.Height = 0;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = g_d3d11.swap_chain_flags;

        hr = IDXGIFactory2_CreateSwapChainForHwnd(
            factory, (IUnknown*)g_d3d11.device, g_ddraw.hwnd, &desc, NULL, NULL, &g_d3d11.swap_chain);

        if (FAILED(hr))
        {
            /* Older Wine */
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.Flags = g_d3d11.swap_chain_flags = 0;
            g_d3d11.tearing = FALSE;

            hr = IDXGIFactory2_CreateSwapChainForHwnd(
                factory, (IUnknown*)g_d3d11.device, g_ddraw.hwnd, &desc, NULL, NULL, &g_d3d11.swap_chain);
        }

        if (SUCCEEDED(hr))
        {
            /* Alt+Enter and window changes are ours, not DXGI's */
            IDXGIFactory2_MakeWindowAssociation(
                factory, g_ddraw.hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

            if (SUCCEEDED(ID3D11Device_QueryInterface(g_d3d11.device, &D3D11_IID_IDXGIDevice1, (void**)&dxgi_device1)))
            {
                IDXGIDevice1_SetMaximumFrameLatency(dxgi_device1, 1);
                IDXGIDevice1_Release(dxgi_device1);
            }

            g_d3d11.hwnd = g_ddraw.hwnd;
            ok = TRUE;
        }
        else
        {
            TRACE("Direct3D11: CreateSwapChainForHwnd failed %08x\n", (unsigned)hr);
        }
    }

    SAFE_RELEASE(factory);
    SAFE_RELEASE(adapter);
    SAFE_RELEASE(dxgi_device);

    if (!ok)
        d3d11_release();

    return ok;
}

BOOL d3d11_release()
{
    if (g_ddraw.render.thread)
        return FALSE;

    d3d11_release_resources();

    SAFE_RELEASE(g_d3d11.back_rtv);
    g_d3d11.back_width = g_d3d11.back_height = 0;

    if (g_d3d11.ctx)
    {
        ID3D11DeviceContext_ClearState(g_d3d11.ctx);
        ID3D11DeviceContext_Flush(g_d3d11.ctx);
    }

    SAFE_RELEASE(g_d3d11.swap_chain);
    SAFE_RELEASE(g_d3d11.ctx);
    SAFE_RELEASE(g_d3d11.device);
    g_d3d11.hwnd = NULL;

    return TRUE;
}

/* --------------------------------------------------------------- resources */

static BOOL d3d11_create_target(D3D11_TARGET* t, int width, int height)
{
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; /* GL_RGBA8, as the OpenGL framebuffers */
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    t->width = width;
    t->height = height;

    return
        !FAILEDX(ID3D11Device_CreateTexture2D(g_d3d11.device, &desc, NULL, &t->tex)) &&
        !FAILEDX(ID3D11Device_CreateRenderTargetView(g_d3d11.device, (ID3D11Resource*)t->tex, NULL, &t->rtv)) &&
        !FAILEDX(ID3D11Device_CreateShaderResourceView(g_d3d11.device, (ID3D11Resource*)t->tex, NULL, &t->srv));
}

static void d3d11_release_target(D3D11_TARGET* t)
{
    SAFE_RELEASE(t->srv);
    SAFE_RELEASE(t->rtv);
    SAFE_RELEASE(t->tex);
    t->width = t->height = 0;
}

static BOOL d3d11_create_dynamic_texture(
    int width, int height, DXGI_FORMAT format, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv)
{
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    return
        !FAILEDX(ID3D11Device_CreateTexture2D(g_d3d11.device, &desc, NULL, tex)) &&
        !FAILEDX(ID3D11Device_CreateShaderResourceView(g_d3d11.device, (ID3D11Resource*)*tex, NULL, srv));
}

static ID3D11SamplerState* d3d11_create_sampler(D3D11_FILTER filter)
{
    D3D11_SAMPLER_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Filter = filter;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    ID3D11SamplerState* s = NULL;
    FAILEDX(ID3D11Device_CreateSamplerState(g_d3d11.device, &desc, &s));
    return s;
}

static ID3D11Buffer* d3d11_create_cbuffer(unsigned int size)
{
    if (!size)
        return NULL;

    D3D11_BUFFER_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.ByteWidth = (size + 15) & ~15u;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    ID3D11Buffer* b = NULL;
    FAILEDX(ID3D11Device_CreateBuffer(g_d3d11.device, &desc, NULL, &b));
    return b;
}

static BOOL d3d11_create_pass(D3D11_PASS* p, const D3D11_SHADER* shader)
{
    memset(p, 0, sizeof(*p));
    p->shader = shader;

    if (shader->vs_cbuffer_size > sizeof(p->vs_data) || shader->ps_cbuffer_size > sizeof(p->ps_data))
        return FALSE;

    if (FAILEDX(ID3D11Device_CreateVertexShader(g_d3d11.device, shader->vs, shader->vs_size, NULL, &p->vs)) ||
        FAILEDX(ID3D11Device_CreatePixelShader(g_d3d11.device, shader->ps, shader->ps_size, NULL, &p->ps)))
        return FALSE;

    p->vs_cb = d3d11_create_cbuffer(shader->vs_cbuffer_size);
    p->ps_cb = d3d11_create_cbuffer(shader->ps_cbuffer_size);

    return (p->vs_cb || !shader->vs_cbuffer_size) && (p->ps_cb || !shader->ps_cbuffer_size);
}

static void d3d11_release_pass(D3D11_PASS* p)
{
    SAFE_RELEASE(p->vs);
    SAFE_RELEASE(p->ps);
    SAFE_RELEASE(p->vs_cb);
    SAFE_RELEASE(p->ps_cb);
    p->shader = NULL;
}

/* Sets a uniform in whichever stages have it, like glUniform*; uniforms the
 * shader doesn't use are skipped */
static void d3d11_uniform(D3D11_PASS* p, const char* name, const float* f, int i)
{
    for (unsigned int u = 0; u < p->shader->uniform_count; u++)
    {
        const D3D11_UNIFORM* uni = &p->shader->uniforms[u];

        if (strcmp(uni->name, name) != 0)
            continue;

        int count =
            uni->type == D3D11_UNIFORM_VEC2 ? 2 :
            uni->type == D3D11_UNIFORM_VEC3 ? 3 :
            uni->type == D3D11_UNIFORM_VEC4 ? 4 : 1;

        unsigned char* dst[2] = {
            uni->vs_offset >= 0 ? p->vs_data + uni->vs_offset : NULL,
            uni->ps_offset >= 0 ? p->ps_data + uni->ps_offset : NULL,
        };

        for (int s = 0; s < 2; s++)
        {
            if (!dst[s])
                continue;

            if (uni->type == D3D11_UNIFORM_INT)
                memcpy(dst[s], &i, sizeof(int));
            else if (f)
                memcpy(dst[s], f, sizeof(float) * count);
        }

        return;
    }
}

static void d3d11_uniform1f(D3D11_PASS* p, const char* name, float x)
{
    d3d11_uniform(p, name, &x, 0);
}

static void d3d11_uniform2f(D3D11_PASS* p, const char* name, float x, float y)
{
    float v[2] = { x, y };
    d3d11_uniform(p, name, v, 0);
}

static void d3d11_uniform1i(D3D11_PASS* p, const char* name, int i)
{
    d3d11_uniform(p, name, NULL, i);
}

static void d3d11_upload_pass(D3D11_PASS* p)
{
    if (p->vs_cb)
        ID3D11DeviceContext_UpdateSubresource(g_d3d11.ctx, (ID3D11Resource*)p->vs_cb, 0, NULL, p->vs_data, 0, 0);

    if (p->ps_cb)
        ID3D11DeviceContext_UpdateSubresource(g_d3d11.ctx, (ID3D11Resource*)p->ps_cb, 0, NULL, p->ps_data, 0, 0);
}

static char* d3d11_load_text_resource(const char* name)
{
    HRSRC res = FindResourceA(g_ddraw_module, name, MAKEINTRESOURCEA(10) /* RT_RCDATA */);
    HGLOBAL mem = res ? LoadResource(g_ddraw_module, res) : NULL;
    DWORD size = res ? SizeofResource(g_ddraw_module, res) : 0;
    const void* data = mem ? LockResource(mem) : NULL;

    if (!data)
        return NULL;

    char* text = malloc(size + 1);
    if (text)
    {
        memcpy(text, data, size);
        text[size] = 0;
    }

    return text;
}

/* The shader crt-auto chose, with the uniforms DOSBox Staging's shader
 * pipeline sets (shader_pipeline.cpp); as ogl_init_crt_auto_uniforms */
static BOOL d3d11_create_crt_auto()
{
    const CRT_AUTO_LAYOUT* crt = &g_ddraw.render.crt;
    const D3D11_SHADER* shader = NULL;
    const char* shader_res = NULL;
    const char* preset_res = NULL;

    switch (crt->shader)
    {
    case CRT_SHADER_VGA_1080P:
        shader = &d3d11_vga_1080p;
        shader_res = "CRT_VGA_1080P";
        break;
    case CRT_SHADER_VGA_1080P_FAKE_DOUBLE_SCAN:
        shader = &d3d11_vga_1080p_fake_double_scan;
        shader_res = "CRT_VGA_1080P_FAKE_DOUBLE_SCAN";
        break;
    case CRT_SHADER_HYLLIAN_VGA_1440P:
        shader = &d3d11_crt_hyllian;
        shader_res = "CRT_HYLLIAN";
        preset_res = "CRT_PRESET_VGA_1440P";
        break;
    case CRT_SHADER_HYLLIAN_VGA_4K:
        shader = &d3d11_crt_hyllian;
        shader_res = "CRT_HYLLIAN";
        preset_res = "CRT_PRESET_VGA_4K";
        break;
    case CRT_SHADER_SHARP:
    default:
        shader = &d3d11_sharp;
        shader_res = "CRT_SHARP";
        break;
    }

    /* The '#pragma parameter' defaults come from the GLSL source the
     * shader was translated from, then the preset */
    char* source = d3d11_load_text_resource(shader_res);
    char* preset = preset_res ? d3d11_load_text_resource(preset_res) : NULL;

    memset(&g_d3d11.crt_params, 0, sizeof(g_d3d11.crt_params));

    BOOL ok = source && (preset || !preset_res);

    if (ok)
    {
        crt_auto_params_from_shader(&g_d3d11.crt_params, source);

        if (preset)
            crt_auto_params_apply_preset(&g_d3d11.crt_params, preset);
    }

    free(source);
    free(preset);

    ok = ok &&
        d3d11_create_pass(&g_d3d11.adj_pass, &d3d11_image_adjustments) &&
        d3d11_create_pass(&g_d3d11.crt_pass, shader) &&
        d3d11_create_target(&g_d3d11.adj, crt->render_width, crt->render_height);

    if (!ok)
    {
        TRACE("     crt-auto: Direct3D11 setup for %s failed\n", crt_auto_shader_name(crt->shader));
        return FALSE;
    }

    CRT_AUTO_COLOR color = crt_auto_vga_color(&g_config.crt);
    D3D11_PASS* p = &g_d3d11.adj_pass;

    d3d11_uniform2f(p, "INPUT_SIZE_0", (float)g_ddraw.width, (float)g_ddraw.height);
    d3d11_uniform2f(p, "OUTPUT_START_OFFSET", 0.0f, 0.0f);
    d3d11_uniform2f(p, "OUTPUT_SIZE", (float)crt->render_width, (float)crt->render_height);

    d3d11_uniform1i(p, "COLOR_SPACE", color.color_space);
    d3d11_uniform1i(p, "ENABLE_ADJUSTMENTS", color.enable_adjustments);
    d3d11_uniform1i(p, "COLOR_PROFILE", color.color_profile);
    d3d11_uniform1f(p, "BRIGHTNESS", color.brightness);
    d3d11_uniform1f(p, "CONTRAST", color.contrast);
    d3d11_uniform1f(p, "GAMMA", color.gamma);
    d3d11_uniform1f(p, "DIGITAL_CONTRAST", color.digital_contrast);
    d3d11_uniform(p, "BLACK_LEVEL_COLOR", color.black_level_color, 0);
    d3d11_uniform1f(p, "BLACK_LEVEL", color.black_level);
    d3d11_uniform1f(p, "SATURATION", color.saturation);
    d3d11_uniform1f(p, "COLOR_TEMPERATURE_KELVIN", color.color_temperature_kelvin);
    d3d11_uniform1f(p, "COLOR_TEMPERATURE_LUMA_PRESERVE", color.color_temperature_luma_preserve);
    d3d11_uniform1f(p, "RED_GAIN", color.red_gain);
    d3d11_uniform1f(p, "GREEN_GAIN", color.green_gain);
    d3d11_uniform1f(p, "BLUE_GAIN", color.blue_gain);

    p = &g_d3d11.crt_pass;

    d3d11_uniform2f(p, "INPUT_SIZE_0", (float)crt->render_width, (float)crt->render_height);

    /* OpenGL window coordinates (bottom-left origin), as DOSBox passes its
     * draw rectangle to glViewport */
    d3d11_uniform2f(p, "OUTPUT_START_OFFSET", (float)g_ddraw.render.viewport.x, (float)g_ddraw.render.viewport.y);
    d3d11_uniform2f(p, "OUTPUT_SIZE", (float)g_ddraw.render.viewport.width, (float)g_ddraw.render.viewport.height);

    for (int i = 0; i < g_d3d11.crt_params.count; i++)
    {
        d3d11_uniform1f(p, g_d3d11.crt_params.param[i].name, g_d3d11.crt_params.param[i].value);
    }

    d3d11_upload_pass(&g_d3d11.adj_pass);
    d3d11_upload_pass(&g_d3d11.crt_pass);

    TRACE("     crt-auto: using %s (Direct3D11)\n", crt_auto_shader_name(crt->shader));

    return TRUE;
}

static BOOL d3d11_create_resources()
{
    d3d11_release_resources();

    if (!g_d3d11.device)
        return FALSE;

    /* The quad the DOSBox passes draw, as a triangle strip */
    static const float quad[] = { -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f };

    D3D11_BUFFER_DESC bd;
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = sizeof(quad);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA init = { quad, 0, 0 };

    if (FAILEDX(ID3D11Device_CreateBuffer(g_d3d11.device, &bd, &init, &g_d3d11.quad_vb)))
        return FALSE;

    /* Every vertex shader here takes 'a_position' as TEXCOORD0 */
    D3D11_INPUT_ELEMENT_DESC ied = {
        "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
    };

    if (FAILEDX(ID3D11Device_CreateInputLayout(
            g_d3d11.device, &ied, 1, d3d11_vs_quad, sizeof(d3d11_vs_quad), &g_d3d11.layout)) ||
        FAILEDX(ID3D11Device_CreateVertexShader(
            g_d3d11.device, d3d11_vs_quad, sizeof(d3d11_vs_quad), NULL, &g_d3d11.vs_quad)) ||
        FAILEDX(ID3D11Device_CreatePixelShader(
            g_d3d11.device, d3d11_ps_plain, sizeof(d3d11_ps_plain), NULL, &g_d3d11.ps_plain)))
        return FALSE;

    /* The quad's winding flips with y */
    D3D11_RASTERIZER_DESC rd;
    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;

    if (FAILEDX(ID3D11Device_CreateRasterizerState(g_d3d11.device, &rd, &g_d3d11.raster)))
        return FALSE;

    g_d3d11.point = d3d11_create_sampler(D3D11_FILTER_MIN_MAG_MIP_POINT);
    g_d3d11.linear = d3d11_create_sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR);

    if (!g_d3d11.point || !g_d3d11.linear)
        return FALSE;

    /* The game's surface, converted by ps_convert_* */
    const unsigned char* ps = NULL;
    SIZE_T ps_size = 0;
    DXGI_FORMAT format;

    if (g_ddraw.bpp == 8)
    {
        format = DXGI_FORMAT_R8_UINT;
        ps = d3d11_ps_convert_pal8;
        ps_size = sizeof(d3d11_ps_convert_pal8);
    }
    else if (g_ddraw.bpp == 16)
    {
        format = DXGI_FORMAT_R16_UINT;
        ps = g_config.rgb555 ? d3d11_ps_convert_rgb555 : d3d11_ps_convert_rgb565;
        ps_size = g_config.rgb555 ? sizeof(d3d11_ps_convert_rgb555) : sizeof(d3d11_ps_convert_rgb565);
    }
    else
    {
        format = DXGI_FORMAT_B8G8R8A8_UNORM;
        ps = d3d11_ps_convert_bgra32;
        ps_size = sizeof(d3d11_ps_convert_bgra32);
    }

    if (FAILEDX(ID3D11Device_CreatePixelShader(g_d3d11.device, ps, ps_size, NULL, &g_d3d11.ps_convert)) ||
        !d3d11_create_dynamic_texture(
            g_ddraw.width, g_ddraw.height, format, &g_d3d11.surface_tex, &g_d3d11.surface_srv))
        return FALSE;

    /* data_bgr is R, G, B, 0 in memory */
    if (g_ddraw.bpp == 8 &&
        !d3d11_create_dynamic_texture(
            256, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &g_d3d11.palette_tex, &g_d3d11.palette_srv))
        return FALSE;

    if (!d3d11_create_target(&g_d3d11.conv, g_ddraw.width, g_ddraw.height))
        return FALSE;

    g_d3d11.crt_auto = g_config.crt_auto && d3d11_create_crt_auto();

    if (g_d3d11.crt_auto && g_d3d11.crt_pass.shader->window_pass)
    {
        g_d3d11.window_cb = d3d11_create_cbuffer(sizeof(g_d3d11.window_cb_data));
        memset(g_d3d11.window_cb_data, 0, sizeof(g_d3d11.window_cb_data));

        if (!g_d3d11.window_cb)
            return FALSE;
    }

#ifdef _DEBUG
    D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
    ID3D11Device_CreateQuery(g_d3d11.device, &qd, &g_d3d11.gpu_disjoint);
    qd.Query = D3D11_QUERY_TIMESTAMP;
    ID3D11Device_CreateQuery(g_d3d11.device, &qd, &g_d3d11.gpu_start);
    ID3D11Device_CreateQuery(g_d3d11.device, &qd, &g_d3d11.gpu_end);
    g_d3d11.gpu_time_pending = FALSE;
    g_d3d11.gpu_time_ms = 0;
    g_d3d11.gpu_time_frames = 0;
#endif

    InterlockedExchange(&g_ddraw.render.surface_updated, TRUE);
    InterlockedExchange(&g_ddraw.render.palette_updated, TRUE);

    return TRUE;
}

static void d3d11_release_resources()
{
    if (g_d3d11.ctx)
        ID3D11DeviceContext_ClearState(g_d3d11.ctx);

#ifdef _DEBUG
    SAFE_RELEASE(g_d3d11.gpu_disjoint);
    SAFE_RELEASE(g_d3d11.gpu_start);
    SAFE_RELEASE(g_d3d11.gpu_end);
#endif

    d3d11_release_pass(&g_d3d11.adj_pass);
    d3d11_release_pass(&g_d3d11.crt_pass);
    d3d11_release_target(&g_d3d11.conv);
    d3d11_release_target(&g_d3d11.adj);
    SAFE_RELEASE(g_d3d11.window_cb);

    SAFE_RELEASE(g_d3d11.palette_srv);
    SAFE_RELEASE(g_d3d11.palette_tex);
    SAFE_RELEASE(g_d3d11.surface_srv);
    SAFE_RELEASE(g_d3d11.surface_tex);
    SAFE_RELEASE(g_d3d11.ps_convert);
    SAFE_RELEASE(g_d3d11.ps_plain);
    SAFE_RELEASE(g_d3d11.vs_quad);
    SAFE_RELEASE(g_d3d11.linear);
    SAFE_RELEASE(g_d3d11.point);
    SAFE_RELEASE(g_d3d11.raster);
    SAFE_RELEASE(g_d3d11.layout);
    SAFE_RELEASE(g_d3d11.quad_vb);

    g_d3d11.crt_auto = FALSE;
}

/* Keeps the swap chain the size of the client area. FALSE while
 * there's nothing to draw into (minimized) */
static BOOL d3d11_update_back_buffer()
{
    RECT rc;
    if (!real_GetClientRect(g_d3d11.hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0)
        return FALSE;

    if (g_d3d11.back_rtv && rc.right == g_d3d11.back_width && rc.bottom == g_d3d11.back_height)
        return TRUE;

    SAFE_RELEASE(g_d3d11.back_rtv);

    ID3D11DeviceContext_OMSetRenderTargets(g_d3d11.ctx, 0, NULL, NULL);
    ID3D11DeviceContext_Flush(g_d3d11.ctx);

    if (FAILEDX(IDXGISwapChain1_ResizeBuffers(
        g_d3d11.swap_chain, 0, rc.right, rc.bottom, DXGI_FORMAT_UNKNOWN, g_d3d11.swap_chain_flags)))
        return FALSE;

    ID3D11Texture2D* back = NULL;

    if (FAILEDX(IDXGISwapChain1_GetBuffer(g_d3d11.swap_chain, 0, &D3D11_IID_ID3D11Texture2D, (void**)&back)))
        return FALSE;

    HRESULT hr = ID3D11Device_CreateRenderTargetView(g_d3d11.device, (ID3D11Resource*)back, NULL, &g_d3d11.back_rtv);
    ID3D11Texture2D_Release(back);

    if (FAILEDX(hr))
        return FALSE;

    g_d3d11.back_width = rc.right;
    g_d3d11.back_height = rc.bottom;

    return TRUE;
}

/* ------------------------------------------------------------------ render */

static void d3d11_set_viewport(float x, float y, float w, float h)
{
    D3D11_VIEWPORT vp = { x, y, w, h, 0.0f, 1.0f };
    ID3D11DeviceContext_RSSetViewports(g_d3d11.ctx, 1, &vp);
}

static void d3d11_draw_pass(
    ID3D11RenderTargetView* rtv,
    ID3D11VertexShader* vs,
    ID3D11PixelShader* ps,
    D3D11_PASS* pass,
    ID3D11ShaderResourceView* srv0,
    ID3D11ShaderResourceView* srv1,
    ID3D11SamplerState* sampler)
{
    ID3D11ShaderResourceView* none[2] = { NULL, NULL };
    ID3D11DeviceContext_PSSetShaderResources(g_d3d11.ctx, 0, 2, none);

    ID3D11DeviceContext_OMSetRenderTargets(g_d3d11.ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_VSSetShader(g_d3d11.ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(g_d3d11.ctx, ps, NULL, 0);

    if (pass)
    {
        UINT slot = pass->shader->texture_slot;
        ID3D11DeviceContext_VSSetConstantBuffers(g_d3d11.ctx, 0, 1, &pass->vs_cb);
        ID3D11DeviceContext_PSSetConstantBuffers(g_d3d11.ctx, 0, 1, &pass->ps_cb);

        if (pass->shader->window_pass)
            ID3D11DeviceContext_PSSetConstantBuffers(g_d3d11.ctx, 1, 1, &g_d3d11.window_cb);
        ID3D11DeviceContext_PSSetShaderResources(g_d3d11.ctx, slot, 1, &srv0);
        ID3D11DeviceContext_PSSetSamplers(g_d3d11.ctx, slot, 1, &sampler);
    }
    else
    {
        ID3D11ShaderResourceView* srvs[2] = { srv0, srv1 };
        ID3D11DeviceContext_PSSetShaderResources(g_d3d11.ctx, 0, 2, srvs);
        ID3D11DeviceContext_PSSetSamplers(g_d3d11.ctx, 0, 1, &sampler);
    }

    ID3D11DeviceContext_Draw(g_d3d11.ctx, 4, 0);
}

#ifdef _DEBUG
/* GPU time of the crt-auto passes, logged as an average every 300 frames */
static BOOL d3d11_gpu_time_begin()
{
    if (!g_d3d11.gpu_disjoint || !g_d3d11.gpu_start || !g_d3d11.gpu_end)
        return FALSE;

    if (g_d3d11.gpu_time_pending)
    {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        UINT64 t0, t1;

        if (ID3D11DeviceContext_GetData(
                g_d3d11.ctx, (ID3D11Asynchronous*)g_d3d11.gpu_disjoint, &dj, sizeof(dj),
                D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            ID3D11DeviceContext_GetData(
                g_d3d11.ctx, (ID3D11Asynchronous*)g_d3d11.gpu_start, &t0, sizeof(t0),
                D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            ID3D11DeviceContext_GetData(
                g_d3d11.ctx, (ID3D11Asynchronous*)g_d3d11.gpu_end, &t1, sizeof(t1),
                D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            return FALSE;

        g_d3d11.gpu_time_pending = FALSE;

        if (dj.Disjoint || !dj.Frequency)
            return FALSE;

        g_d3d11.gpu_time_ms += (double)(t1 - t0) * 1000.0 / (double)dj.Frequency;

        if (++g_d3d11.gpu_time_frames == 300)
        {
            TRACE(
                "crt-auto: GPU time %.3f ms per frame (%s, %ux%u to %dx%d, average of 300 frames, Direct3D11)\n",
                g_d3d11.gpu_time_ms / g_d3d11.gpu_time_frames,
                crt_auto_shader_name(g_ddraw.render.crt.shader),
                g_ddraw.width,
                g_ddraw.height,
                g_ddraw.render.viewport.width,
                g_ddraw.render.viewport.height);

            g_d3d11.gpu_time_ms = 0;
            g_d3d11.gpu_time_frames = 0;
        }
    }

    ID3D11DeviceContext_Begin(g_d3d11.ctx, (ID3D11Asynchronous*)g_d3d11.gpu_disjoint);
    ID3D11DeviceContext_End(g_d3d11.ctx, (ID3D11Asynchronous*)g_d3d11.gpu_start);
    return TRUE;
}

static void d3d11_gpu_time_end()
{
    ID3D11DeviceContext_End(g_d3d11.ctx, (ID3D11Asynchronous*)g_d3d11.gpu_end);
    ID3D11DeviceContext_End(g_d3d11.ctx, (ID3D11Asynchronous*)g_d3d11.gpu_disjoint);
    g_d3d11.gpu_time_pending = TRUE;
}
#endif

static void d3d11_draw_frame()
{
    static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    UINT stride = sizeof(float) * 2, offset = 0;

    ID3D11DeviceContext_IASetInputLayout(g_d3d11.ctx, g_d3d11.layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_d3d11.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_IASetVertexBuffers(g_d3d11.ctx, 0, 1, &g_d3d11.quad_vb, &stride, &offset);
    ID3D11DeviceContext_RSSetState(g_d3d11.ctx, g_d3d11.raster);

#ifdef _DEBUG
    BOOL gpu_timing = g_d3d11.crt_auto && d3d11_gpu_time_begin();
#endif

    /* game surface -> conv */
    d3d11_set_viewport(0.0f, 0.0f, (float)g_ddraw.width, (float)g_ddraw.height);
    d3d11_draw_pass(
        g_d3d11.conv.rtv, g_d3d11.vs_quad, g_d3d11.ps_convert, NULL,
        g_d3d11.surface_srv, g_d3d11.palette_srv, g_d3d11.point);

    if (g_d3d11.crt_auto)
    {
        /* image adjustments: conv -> adj, read nearest (linear_filtering off) */
        d3d11_set_viewport(0.0f, 0.0f, (float)g_d3d11.adj.width, (float)g_d3d11.adj.height);
        d3d11_draw_pass(
            g_d3d11.adj.rtv, g_d3d11.adj_pass.vs, g_d3d11.adj_pass.ps, &g_d3d11.adj_pass,
            g_d3d11.conv.srv, NULL, g_d3d11.point);

        /* CRT shader: adj -> back buffer. The rectangle is the OpenGL
         * renderer's glViewport, bottom-left origin, turned round. */
        ID3D11DeviceContext_ClearRenderTargetView(g_d3d11.ctx, g_d3d11.back_rtv, black);

        int x, y, w, h;

        if (g_ddraw.child_window_exists)
        {
            x = 0;
            y = g_ddraw.render.height - g_ddraw.height;
            w = g_ddraw.width;
            h = g_ddraw.height;
        }
        else
        {
            x = g_ddraw.render.viewport.x;
            y = g_ddraw.render.viewport.y;
            w = g_ddraw.render.viewport.width;
            h = g_ddraw.render.viewport.height;
        }

        d3d11_set_viewport((float)x, (float)(g_d3d11.back_height - y - h), (float)w, (float)h);

        /* The DirectCRT cbuffer (gen_d3d11_shaders.py, WINDOW_CBUFFER) */
        float cb[8] = {
            (float)x, (float)y, (float)w, (float)h,
            (float)g_ddraw.render.crt.render_width, (float)g_ddraw.render.crt.render_height,
            (float)g_d3d11.back_height, 0.0f
        };

        if (g_d3d11.window_cb && memcmp(cb, g_d3d11.window_cb_data, sizeof(cb)) != 0)
        {
            ID3D11DeviceContext_UpdateSubresource(
                g_d3d11.ctx, (ID3D11Resource*)g_d3d11.window_cb, 0, NULL, cb, 0, 0);
            memcpy(g_d3d11.window_cb_data, cb, sizeof(cb));
        }

        d3d11_draw_pass(
            g_d3d11.back_rtv, g_d3d11.crt_pass.vs, g_d3d11.crt_pass.ps, &g_d3d11.crt_pass,
            g_d3d11.adj.srv, NULL, g_d3d11.linear);
    }
    else
    {
        ID3D11DeviceContext_ClearRenderTargetView(g_d3d11.ctx, g_d3d11.back_rtv, black);

        BOOL scaled;

        if (g_ddraw.child_window_exists)
        {
            d3d11_set_viewport(0.0f, 0.0f, (float)g_ddraw.width, (float)g_ddraw.height);
            scaled = FALSE;
        }
        else
        {
            d3d11_set_viewport(
                (float)g_ddraw.render.viewport.x,
                (float)g_ddraw.render.viewport.y,
                (float)g_ddraw.render.viewport.width,
                (float)g_ddraw.render.viewport.height);

            scaled =
                g_ddraw.render.viewport.width != g_ddraw.width ||
                g_ddraw.render.viewport.height != g_ddraw.height;
        }

        d3d11_draw_pass(
            g_d3d11.back_rtv, g_d3d11.vs_quad, g_d3d11.ps_plain, NULL,
            g_d3d11.conv.srv, NULL, scaled && g_config.d3d9_filter ? g_d3d11.linear : g_d3d11.point);
    }

#ifdef _DEBUG
    if (gpu_timing)
        d3d11_gpu_time_end();
#endif

    if (g_ddraw.bnet_active)
        ID3D11DeviceContext_ClearRenderTargetView(g_d3d11.ctx, g_d3d11.back_rtv, black);

    ID3D11ShaderResourceView* none[2] = { NULL, NULL };
    ID3D11DeviceContext_PSSetShaderResources(g_d3d11.ctx, 0, 2, none);
}

static void d3d11_upload()
{
    D3D11_MAPPED_SUBRESOURCE map;

    if (InterlockedExchange(&g_ddraw.render.surface_updated, FALSE) || g_config.minfps == -2)
    {
        if (SUCCEEDED(ID3D11DeviceContext_Map(
            g_d3d11.ctx, (ID3D11Resource*)g_d3d11.surface_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
        {
            blt_clean(
                map.pData,
                0,
                0,
                g_ddraw.primary->width,
                g_ddraw.primary->height,
                map.RowPitch,
                g_ddraw.primary->surface,
                0,
                0,
                g_ddraw.primary->pitch,
                g_ddraw.primary->bpp);

            ID3D11DeviceContext_Unmap(g_d3d11.ctx, (ID3D11Resource*)g_d3d11.surface_tex, 0);
        }
    }

    if (g_ddraw.bpp == 8 &&
        (InterlockedExchange(&g_ddraw.render.palette_updated, FALSE) || g_config.minfps == -2))
    {
        if (SUCCEEDED(ID3D11DeviceContext_Map(
            g_d3d11.ctx, (ID3D11Resource*)g_d3d11.palette_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
        {
            memcpy(map.pData, g_ddraw.primary->palette->data_bgr, 256 * sizeof(int));
            ID3D11DeviceContext_Unmap(g_d3d11.ctx, (ID3D11Resource*)g_d3d11.palette_tex, 0);
        }
    }
}

/* Device removed (driver update or crash, GPU switch): start again */
static BOOL d3d11_recover()
{
    TRACE("Direct3D11: device lost (%08x), recreating\n",
        g_d3d11.device ? (unsigned)ID3D11Device_GetDeviceRemovedReason(g_d3d11.device) : 0);

    d3d11_release_resources();
    SAFE_RELEASE(g_d3d11.back_rtv);
    g_d3d11.back_width = g_d3d11.back_height = 0;
    SAFE_RELEASE(g_d3d11.swap_chain);
    SAFE_RELEASE(g_d3d11.ctx);
    SAFE_RELEASE(g_d3d11.device);

    return d3d11_create_device() && d3d11_create_resources();
}

static void d3d11_render()
{
    DWORD timeout = g_config.minfps > 0 ? g_ddraw.minfps_tick_len : INFINITE;

    while (g_ddraw.render.run &&
        (g_config.minfps < 0 || WaitForSingleObject(g_ddraw.render.sem, timeout) != WAIT_FAILED) &&
        g_ddraw.render.run)
    {
#if _DEBUG
        dbg_draw_frame_info_start();
#endif

        fpsl_frame_start();

        BOOL can_draw = d3d11_update_back_buffer();

        EnterCriticalSection(&g_ddraw.cs);

        if (g_ddraw.primary &&
            g_ddraw.primary->bpp == g_ddraw.bpp &&
            g_ddraw.primary->width == g_ddraw.width &&
            g_ddraw.primary->height == g_ddraw.height &&
            (g_ddraw.bpp == 16 || g_ddraw.bpp == 32 || g_ddraw.primary->palette))
        {
            if (g_config.lock_surfaces)
                EnterCriticalSection(&g_ddraw.primary->cs);

            d3d11_upload();

            if (g_config.fixchilds)
            {
                g_ddraw.child_window_exists = FALSE;
                EnumChildWindows(g_ddraw.hwnd, util_enum_child_proc, (LPARAM)g_ddraw.primary);
            }

            if (g_config.lock_surfaces)
                LeaveCriticalSection(&g_ddraw.primary->cs);
        }

        LeaveCriticalSection(&g_ddraw.cs);

        if (can_draw)
        {
            d3d11_draw_frame();

            UINT sync = g_config.vsync ? 1 : 0;
            UINT flags = !sync && g_d3d11.tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;

            HRESULT hr = IDXGISwapChain1_Present(g_d3d11.swap_chain, sync, flags);

            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            {
                if (!d3d11_recover())
                    break;
            }
            else if (!g_ddraw.primary)
            {
                /* Force redraw for GDI games (ClueFinders) */
                RedrawWindow(g_ddraw.hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
        }
        else
        {
            Sleep(10);
        }

        if (!g_ddraw.render.run)
            break;

#if _DEBUG
        dbg_draw_frame_info_end();
#endif

        fpsl_frame_end();
    }
}

DWORD WINAPI d3d11_render_main(void)
{
    Sleep(250);

    fpsl_init();

    BOOL ok = g_d3d11.device && g_d3d11.hwnd == g_ddraw.hwnd && d3d11_create_resources();

    if (ok)
        d3d11_render();

    d3d11_release_resources();

    if (g_d3d11.ctx)
        ID3D11DeviceContext_Flush(g_d3d11.ctx);

    if (!ok)
    {
        TRACE("Direct3D11: setup failed, using GDI\n");
        g_ddraw.show_driver_warning = TRUE;
        g_ddraw.renderer = gdi_render_main;
        gdi_render_main();
    }

    return 0;
}
