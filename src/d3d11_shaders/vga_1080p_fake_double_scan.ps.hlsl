// Generated from crt/vga-1080p-fake-double-scan.glsl by gen_d3d11_shaders.py
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
    float2 _122_INPUT_SIZE_0 : packoffset(c0);
    float2 _122_OUTPUT_START_OFFSET : packoffset(c0.z);
    float _122_PHOSPHOR_LAYOUT : packoffset(c1);
    float _122_SCANLINE_STRENGTH_MIN : packoffset(c1.y);
    float _122_SCANLINE_STRENGTH_MAX : packoffset(c1.z);
    float _122_COLOR_BOOST_EVEN : packoffset(c1.w);
    float _122_COLOR_BOOST_ODD : packoffset(c2);
    float _122_MASK_STRENGTH : packoffset(c2.y);
    float _122_GAMMA_INPUT : packoffset(c2.z);
    float _122_GAMMA_OUTPUT : packoffset(c2.w);
};

Texture2D<float4> INPUT_TEXTURE_0 : register(t1);
SamplerState _INPUT_TEXTURE_0_sampler : register(s1);

static float4 gl_FragCoord;
static float2 v_texCoord;
static float2 prescale;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 v_texCoord : TEXCOORD0;
    float2 prescale : TEXCOORD1;
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

float4 tex2D_linear(Texture2D<float4> sampler_, SamplerState _sampler__sampler, float2 uv)
{
    float2 texCoord = (uv * _122_INPUT_SIZE_0) - 0.5f.xx;
    float2 s0t0 = floor(texCoord) + 0.5f.xx;
    float2 s0t1 = s0t0 + float2(0.0f, 1.0f);
    float2 s1t0 = s0t0 + float2(1.0f, 0.0f);
    float2 s1t1 = s0t0 + 1.0f.xx;
    float2 invTexSize = 1.0f.xx / _122_INPUT_SIZE_0;
    float4 c_s0t0 = pow(sampler_.Sample(_sampler__sampler, s0t0 * invTexSize), _122_GAMMA_INPUT.xxxx);
    float4 c_s0t1 = pow(sampler_.Sample(_sampler__sampler, s0t1 * invTexSize), _122_GAMMA_INPUT.xxxx);
    float4 c_s1t0 = pow(sampler_.Sample(_sampler__sampler, s1t0 * invTexSize), _122_GAMMA_INPUT.xxxx);
    float4 c_s1t1 = pow(sampler_.Sample(_sampler__sampler, s1t1 * invTexSize), _122_GAMMA_INPUT.xxxx);
    float2 weight = frac(texCoord);
    float4 c0 = c_s0t0 + ((c_s1t0 - c_s0t0) * weight.x);
    float4 c1 = c_s0t1 + ((c_s1t1 - c_s0t1) * weight.x);
    return c0 + ((c1 - c0) * weight.y);
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
    float2 mask_coords = gl_FragCoord.xy - _122_OUTPUT_START_OFFSET;
    float3 lum_factors = float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f);
    float luminance = dot(lum_factors, color.xyz);
    float even_odd = floor(mod(mask_coords.y, 2.0f));
    float dim_factor = lerp(1.0f - scanlineStrengthMax, 1.0f - scanlineStrengthMin, luminance);
    float scanline_dim = clamp(even_odd + dim_factor, 0.0f, 1.0f);
    float4 _157 = color;
    float3 _159 = _157.xyz * scanline_dim.xxx;
    color.x = _159.x;
    color.y = _159.y;
    color.z = _159.z;
    float4 _174 = color;
    float3 _176 = _174.xyz * lerp(color_boost_even.xxx, color_boost_odd.xxx, even_odd.xxx);
    color.x = _176.x;
    color.y = _176.y;
    color.z = _176.z;
    float saturation = lerp(1.2000000476837158203125f, 1.0299999713897705078125f, even_odd);
    float l = length(color);
    color.x = pow(color.x + 1.0000000116860974230803549289703e-07f, saturation);
    color.y = pow(color.y + 1.0000000116860974230803549289703e-07f, saturation);
    color.z = pow(color.z + 1.0000000116860974230803549289703e-07f, saturation);
    color = normalize(color) * l;
    float2 param = mask_coords;
    float param_1 = mask_strength;
    int param_2 = int(_122_PHOSPHOR_LAYOUT);
    float4 _224 = color;
    float3 _226 = _224.xyz * mask_weights(param, param_1, param_2);
    color.x = _226.x;
    color.y = _226.y;
    color.z = _226.z;
    return color;
}

void frag_main()
{
    float2 texel_floored = floor(v_texCoord);
    float2 s = frac(v_texCoord);
    float2 region_range = 0.5f.xx - (0.5f.xx / prescale);
    float2 center_dist = s - 0.5f.xx;
    float2 f = ((center_dist - clamp(center_dist, -region_range, region_range)) * prescale) + 0.5f.xx;
    float2 mod_texel = min(texel_floored + f, _122_INPUT_SIZE_0 - 0.5f.xx);
    float2 param = mod_texel / _122_INPUT_SIZE_0;
    float4 color = tex2D_linear(INPUT_TEXTURE_0, _INPUT_TEXTURE_0_sampler, param);
    float4 param_1 = color;
    float param_2 = _122_SCANLINE_STRENGTH_MIN;
    float param_3 = _122_SCANLINE_STRENGTH_MAX;
    float param_4 = _122_COLOR_BOOST_EVEN;
    float param_5 = _122_COLOR_BOOST_ODD;
    float param_6 = _122_MASK_STRENGTH;
    float4 _402 = add_vga_overlay(param_1, param_2, param_3, param_4, param_5, param_6);
    color = _402;
    color = pow(color, (1.0f / _122_GAMMA_OUTPUT).xxxx);
    FragColor = clamp(color, 0.0f.xxxx, 1.0f.xxxx);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.y = DIRECTCRT_WINDOW_HEIGHT - gl_FragCoord.y;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    float2 directcrt_pos = float2(stage_input.gl_FragCoord.x,
        DIRECTCRT_WINDOW_HEIGHT - stage_input.gl_FragCoord.y - (1.0 / 1024.0));
    v_texCoord = (directcrt_pos - DIRECTCRT_VIEWPORT.xy) / DIRECTCRT_VIEWPORT.zw * DIRECTCRT_INPUT_SIZE;
    prescale = stage_input.prescale;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
