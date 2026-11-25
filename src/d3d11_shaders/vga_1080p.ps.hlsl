// Generated from crt/vga-1080p.glsl by gen_d3d11_shaders.py
// SPDX-FileCopyrightText:  2020-2026 The DOSBox Staging Team
// SPDX-License-Identifier: MIT

cbuffer DirectCRT : register(b1)
{
    float4 DIRECTCRT_VIEWPORT : packoffset(c0);         // glViewport x, y, w, h
    float2 DIRECTCRT_INPUT_SIZE : packoffset(c1);       // INPUT_SIZE_0
    float DIRECTCRT_WINDOW_HEIGHT : packoffset(c1.z);
};

cbuffer Globals : register(b0)
{
    float2 _114_OUTPUT_START_OFFSET : packoffset(c0);
    float _114_SPOT_WIDTH : packoffset(c0.z);
    float _114_SPOT_HEIGHT : packoffset(c0.w);
    float _114_PHOSPHOR_LAYOUT : packoffset(c1);
    float _114_SCANLINE_STRENGTH_MIN : packoffset(c1.y);
    float _114_SCANLINE_STRENGTH_MAX : packoffset(c1.z);
    float _114_COLOR_BOOST_EVEN : packoffset(c1.w);
    float _114_COLOR_BOOST_ODD : packoffset(c2);
    float _114_MASK_STRENGTH : packoffset(c2.y);
    float _114_GAMMA_INPUT : packoffset(c2.z);
    float _114_GAMMA_OUTPUT : packoffset(c2.w);
};

Texture2D<float4> INPUT_TEXTURE_0 : register(t1);
SamplerState _INPUT_TEXTURE_0_sampler : register(s1);

static float4 gl_FragCoord;
static float2 v_texCoord;
static float4 sourceSize;
static float2 onex;
static float2 oney;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 v_texCoord : TEXCOORD0;
    float4 sourceSize : TEXCOORD1;
    float2 onex : TEXCOORD2;
    float2 oney : TEXCOORD3;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float3 y)
{
    return x - y * floor(x / y);
}

float4 mod(float4 x, float4 y)
{
    return x - y * floor(x / y);
}

float3 mask_weights(float2 coord, float mask_intensity, int phosphor_layout)
{
    float3 weights = 1.0f.xxx;
    float on = 1.0f;
    float off = 1.0f - mask_intensity;
    float3 green = float3(off, on, off);
    float3 magenta = float3(on, off, on);
    float3 aperture_weights = lerp(magenta, green, floor(mod(coord.x, 2.0f)).xxx);
    if (phosphor_layout == 0)
    {
        return weights;
    }
    else
    {
        if (phosphor_layout == 1)
        {
            weights = aperture_weights;
            return weights;
        }
        else
        {
            if (phosphor_layout == 2)
            {
                float3 inverse_aperture = lerp(green, magenta, floor(mod(coord.x, 2.0f)).xxx);
                weights = lerp(aperture_weights, inverse_aperture, floor(mod(coord.y, 2.0f)).xxx);
                return weights;
            }
            else
            {
                return weights;
            }
        }
    }
}

float4 add_vga_overlay(inout float4 color, float scanlineStrengthMin, float scanlineStrengthMax, float color_boost_even, float color_boost_odd, float mask_strength)
{
    float2 mask_coords = gl_FragCoord.xy - _114_OUTPUT_START_OFFSET;
    float3 lum_factors = float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f);
    float luminance = dot(lum_factors, color.xyz);
    float even_odd = floor(mod(mask_coords.y, 2.0f));
    float dim_factor = lerp(1.0f - scanlineStrengthMax, 1.0f - scanlineStrengthMin, luminance);
    float scanline_dim = clamp(even_odd + dim_factor, 0.0f, 1.0f);
    float4 _149 = color;
    float3 _151 = _149.xyz * scanline_dim.xxx;
    color.x = _151.x;
    color.y = _151.y;
    color.z = _151.z;
    float4 _166 = color;
    float3 _168 = _166.xyz * lerp(color_boost_even.xxx, color_boost_odd.xxx, even_odd.xxx);
    color.x = _168.x;
    color.y = _168.y;
    color.z = _168.z;
    float saturation = lerp(1.2000000476837158203125f, 1.0299999713897705078125f, even_odd);
    float l = length(color);
    color.x = pow(color.x + 1.0000000116860974230803549289703e-07f, saturation);
    color.y = pow(color.y + 1.0000000116860974230803549289703e-07f, saturation);
    color.z = pow(color.z + 1.0000000116860974230803549289703e-07f, saturation);
    color = normalize(color) * l;
    float2 param = mask_coords;
    float param_1 = mask_strength;
    int param_2 = int(_114_PHOSPHOR_LAYOUT);
    float4 _217 = color;
    float3 _219 = _217.xyz * mask_weights(param, param_1, param_2);
    color.x = _219.x;
    color.y = _219.y;
    color.z = _219.z;
    return color;
}

void frag_main()
{
    float2 coords = v_texCoord * sourceSize.xy;
    float2 pixel_center = floor(coords) + 0.5f.xx;
    float2 texture_coords = pixel_center * sourceSize.zw;
    float4 color = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, texture_coords), _114_GAMMA_INPUT.xxxx);
    float dx = coords.x - pixel_center.x;
    float h_weight_00 = dx / _114_SPOT_WIDTH;
    if (h_weight_00 > 1.0f)
    {
        h_weight_00 = 1.0f;
    }
    h_weight_00 = 1.0f - (h_weight_00 * h_weight_00);
    h_weight_00 *= h_weight_00;
    color *= h_weight_00.xxxx;
    float2 coords01;
    if (dx > 0.0f)
    {
        coords01 = onex;
        dx = 1.0f - dx;
    }
    else
    {
        coords01 = -onex;
        dx = 1.0f + dx;
    }
    float4 colorNB = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, texture_coords + coords01), _114_GAMMA_INPUT.xxxx);
    float h_weight_01 = dx / _114_SPOT_WIDTH;
    if (h_weight_01 > 1.0f)
    {
        h_weight_01 = 1.0f;
    }
    h_weight_01 = 1.0f - (h_weight_01 * h_weight_01);
    h_weight_01 *= h_weight_01;
    color += (colorNB * h_weight_01.xxxx);
    float dy = coords.y - pixel_center.y;
    float v_weight_00 = dy / _114_SPOT_HEIGHT;
    if (v_weight_00 > 1.0f)
    {
        v_weight_00 = 1.0f;
    }
    v_weight_00 = 1.0f - (v_weight_00 * v_weight_00);
    v_weight_00 *= v_weight_00;
    color *= v_weight_00.xxxx;
    float2 coords10;
    if (dy > 0.0f)
    {
        coords10 = oney;
        dy = 1.0f - dy;
    }
    else
    {
        coords10 = -oney;
        dy = 1.0f + dy;
    }
    colorNB = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, texture_coords + coords10), _114_GAMMA_INPUT.xxxx);
    float v_weight_10 = dy / _114_SPOT_HEIGHT;
    if (v_weight_10 > 1.0f)
    {
        v_weight_10 = 1.0f;
    }
    v_weight_10 = 1.0f - (v_weight_10 * v_weight_10);
    v_weight_10 *= v_weight_10;
    color += (colorNB * float4(v_weight_10 * h_weight_00, v_weight_10 * h_weight_00, v_weight_10 * h_weight_00, v_weight_10 * h_weight_00));
    colorNB = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, (texture_coords + coords01) + coords10), _114_GAMMA_INPUT.xxxx);
    color += (colorNB * float4(v_weight_10 * h_weight_01, v_weight_10 * h_weight_01, v_weight_10 * h_weight_01, v_weight_10 * h_weight_01));
    float4 param = color;
    float param_1 = _114_SCANLINE_STRENGTH_MIN;
    float param_2 = _114_SCANLINE_STRENGTH_MAX;
    float param_3 = _114_COLOR_BOOST_EVEN;
    float param_4 = _114_COLOR_BOOST_ODD;
    float param_5 = _114_MASK_STRENGTH;
    float4 _465 = add_vga_overlay(param, param_1, param_2, param_3, param_4, param_5);
    color = _465;
    color = pow(color, (1.0f / _114_GAMMA_OUTPUT).xxxx);
    FragColor = clamp(color, 0.0f.xxxx, 1.0f.xxxx);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.y = DIRECTCRT_WINDOW_HEIGHT - gl_FragCoord.y;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    float2 directcrt_pos = float2(stage_input.gl_FragCoord.x,
        DIRECTCRT_WINDOW_HEIGHT - stage_input.gl_FragCoord.y - (1.0 / 1024.0));
    v_texCoord = (directcrt_pos - DIRECTCRT_VIEWPORT.xy) / DIRECTCRT_VIEWPORT.zw;
    sourceSize = stage_input.sourceSize;
    onex = stage_input.onex;
    oney = stage_input.oney;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
