// SPDX-License-Identifier: GPL-2.0-or-later
//
// DirectCRT's own Direct3D 11 passes; compiled by gen_d3d11_shaders.py.
//
// The translated DOSBox passes that draw into a texture keep OpenGL's memory
// layout (their vertex shaders negate y), so every intermediate texture has
// the same rows in the same order as the OpenGL renderer's framebuffers. The
// CRT pass draws straight into the back buffer (see gen_d3d11_shaders.py).

struct VS_OUT
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// a_position is the same quad, -1..1, the DOSBox passes draw.
// uv (0,0) is the top-left of the source, as on screen.
VS_OUT vs_quad(float2 a_position : TEXCOORD0)
{
    VS_OUT o;
    o.pos = float4(a_position, 0.0, 1.0);
    o.uv  = float2(a_position.x + 1.0, 1.0 - a_position.y) * 0.5;
    return o;
}

// ------------------------------------------------------------ conversion
// Game surface -> RGBA8 at game size, one texel per pixel. Row r of the
// output is row r of the surface (the OpenGL renderer's framebuffer 0).

Texture2D<uint>   surface_index : register(t0);
Texture2D<float4> palette       : register(t1);
Texture2D<float4> surface_rgba  : register(t0);

float4 ps_convert_pal8(VS_OUT i) : SV_Target
{
    uint index = surface_index.Load(int3(i.pos.xy, 0));
    return palette.Load(int3(index, 0, 0));
}

float4 ps_convert_rgb565(VS_OUT i) : SV_Target
{
    uint v = surface_index.Load(int3(i.pos.xy, 0));
    return float4(((v >> 11) & 31) / 31.0, ((v >> 5) & 63) / 63.0, (v & 31) / 31.0, 1.0);
}

float4 ps_convert_rgb555(VS_OUT i) : SV_Target
{
    uint v = surface_index.Load(int3(i.pos.xy, 0));
    return float4(((v >> 10) & 31) / 31.0, ((v >> 5) & 31) / 31.0, (v & 31) / 31.0, 1.0);
}

float4 ps_convert_bgra32(VS_OUT i) : SV_Target
{
    return surface_rgba.Load(int3(i.pos.xy, 0));
}
