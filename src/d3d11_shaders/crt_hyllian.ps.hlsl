// Generated from crt/crt-hyllian.glsl by gen_d3d11_shaders.py

cbuffer DirectCRT : register(b1)
{
    float4 DIRECTCRT_VIEWPORT : packoffset(c0);         // glViewport x, y, w, h
    float2 DIRECTCRT_INPUT_SIZE : packoffset(c1);       // INPUT_SIZE_0
    float DIRECTCRT_WINDOW_HEIGHT : packoffset(c1.z);
};

cbuffer Globals : register(b0)
{
    float2 _372_INPUT_SIZE_0 : packoffset(c0);
    float2 _372_OUTPUT_START_OFFSET : packoffset(c0.z);
    float _372_BEAM_PROFILE : packoffset(c1);
    float _372_HFILTER_PROFILE : packoffset(c1.y);
    float _372_BEAM_MIN_WIDTH : packoffset(c1.z);
    float _372_BEAM_MAX_WIDTH : packoffset(c1.w);
    float _372_SCANLINES_STRENGTH : packoffset(c2);
    float _372_COLOR_BOOST : packoffset(c2.y);
    float _372_SHARPNESS_HACK : packoffset(c2.z);
    float _372_PHOSPHOR_LAYOUT : packoffset(c2.w);
    float _372_MASK_INTENSITY : packoffset(c3);
    float _372_CRT_ANTI_RINGING : packoffset(c3.y);
    float _372_INPUT_GAMMA : packoffset(c3.z);
    float _372_OUTPUT_GAMMA : packoffset(c3.w);
    float _372_VSCANLINES : packoffset(c4);
};

Texture2D<float4> INPUT_TEXTURE_0 : register(t1);
SamplerState _INPUT_TEXTURE_0_sampler : register(s1);

static float4 gl_FragCoord;
static float2 v_texCoord;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 v_texCoord : TEXCOORD0;
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

float4 get_beam_profile()
{
    float4 bp = float4(_372_SCANLINES_STRENGTH, _372_BEAM_MIN_WIDTH, _372_BEAM_MAX_WIDTH, _372_COLOR_BOOST);
    if (_372_BEAM_PROFILE == 1.0f)
    {
        bp = float4(0.62000000476837158203125f, 1.0f, 1.0f, 1.39999997615814208984375f);
    }
    if (_372_BEAM_PROFILE == 2.0f)
    {
        bp = float4(0.7200000286102294921875f, 1.0f, 1.0f, 1.2000000476837158203125f);
    }
    return bp;
}

float4x4 get_hfilter_profile()
{
    float bf = 0.0f;
    float cf = 0.0f;
    if (_372_HFILTER_PROFILE == 1.0f)
    {
        bf = 0.0f;
        cf = 0.5f;
    }
    return float4x4(float4(((-bf) - (6.0f * cf)) / 6.0f, ((3.0f * bf) + (12.0f * cf)) / 6.0f, (((-3.0f) * bf) - (6.0f * cf)) / 6.0f, bf / 6.0f), float4(((12.0f - (9.0f * bf)) - (6.0f * cf)) / 6.0f, (((-18.0f) + (12.0f * bf)) + (6.0f * cf)) / 6.0f, 0.0f, (6.0f - (2.0f * bf)) / 6.0f), float4((-((12.0f - (9.0f * bf)) - (6.0f * cf))) / 6.0f, ((18.0f - (15.0f * bf)) - (12.0f * cf)) / 6.0f, ((3.0f * bf) + (6.0f * cf)) / 6.0f, bf / 6.0f), float4((bf + (6.0f * cf)) / 6.0f, -cf, 0.0f, 0.0f));
}

float3 mask_weights(float2 coord, float mask_intensity, int phosphor_layout)
{
    float3 weights = 1.0f.xxx;
    float on = 1.0f;
    float off = 1.0f - mask_intensity;
    float3 red = float3(on, off, off);
    float3 green = float3(off, on, off);
    float3 blue = float3(off, off, on);
    float3 magenta = float3(on, off, on);
    float3 yellow = float3(on, on, off);
    float3 cyan = float3(off, on, on);
    float3 black = off.xxx;
    float3 white = on.xxx;
    int z = 0;
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
                if (phosphor_layout == 4)
                {
                    weights = lerp(yellow, blue, floor(mod(coord.x, 2.0f)).xxx);
                    return weights;
                }
                else
                {
                    int w;
                    if (phosphor_layout == 9)
                    {
                        float3 _153[4] = { red, yellow, cyan, blue };
                        float3 big_ap_rgb[4] = _153;
                        w = int(floor(mod(coord.x, 4.0f)));
                        weights = big_ap_rgb[w];
                        return weights;
                    }
                    else
                    {
                        if (phosphor_layout == 11)
                        {
                            float3 _177[4] = { red, green, blue, black };
                            float3 delta_1_1[4] = _177;
                            float3 _183[4] = { blue, black, red, green };
                            float3 delta_1_2[4] = _183;
                            w = int(floor(mod(coord.y, 2.0f)));
                            z = int(floor(mod(coord.x, 4.0f)));
                            float3 _196;
                            if (w == 1)
                            {
                                _196 = delta_1_1[z];
                            }
                            else
                            {
                                _196 = delta_1_2[z];
                            }
                            weights = _196;
                            return weights;
                        }
                        else
                        {
                            if (phosphor_layout == 14)
                            {
                                float3 _222[6] = { magenta, green, black, black, black, black };
                                float3 slot2_1[6] = _222;
                                float3 _230[6] = { magenta, green, black, magenta, green, black };
                                float3 slot2_2[6] = _230;
                                float3 _236[6] = { black, black, black, magenta, green, black };
                                float3 slot2_3[6] = _236;
                                w = int(floor(mod(coord.y, 3.0f)));
                                z = int(floor(mod(coord.x, 6.0f)));
                                float3 _251;
                                if (w == 1)
                                {
                                    _251 = slot2_1[z];
                                }
                                else
                                {
                                    float3 _260;
                                    if (w == 2)
                                    {
                                        _260 = slot2_2[z];
                                    }
                                    else
                                    {
                                        _260 = slot2_3[z];
                                    }
                                    _251 = _260;
                                }
                                weights = _251;
                                return weights;
                            }
                            else
                            {
                                if (phosphor_layout == 17)
                                {
                                    float3 _292[10] = { red, magenta, blue, green, green, red, magenta, blue, green, green };
                                    float3 slot_1[10] = _292;
                                    float3 _299[10] = { black, blue, blue, green, green, red, red, black, black, black };
                                    float3 slot_2[10] = _299;
                                    float3 _309[10] = { red, magenta, blue, green, green, red, magenta, blue, green, green };
                                    float3 slot_3[10] = _309;
                                    float3 _315[10] = { red, red, black, black, black, black, blue, blue, green, green };
                                    float3 slot_4[10] = _315;
                                    w = int(floor(mod(coord.y, 4.0f)));
                                    z = int(floor(mod(coord.x, 10.0f)));
                                    float3 _329;
                                    if (w == 1)
                                    {
                                        _329 = slot_1[z];
                                    }
                                    else
                                    {
                                        float3 _338;
                                        if (w == 2)
                                        {
                                            _338 = slot_2[z];
                                        }
                                        else
                                        {
                                            float3 _348;
                                            if (w == 3)
                                            {
                                                _348 = slot_3[z];
                                            }
                                            else
                                            {
                                                _348 = slot_4[z];
                                            }
                                            _338 = _348;
                                        }
                                        _329 = _338;
                                    }
                                    weights = _329;
                                    return weights;
                                }
                                else
                                {
                                    return weights;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void frag_main()
{
    float4 profile = get_beam_profile();
    float2 TextureSize = lerp(float2(_372_INPUT_SIZE_0.x * _372_SHARPNESS_HACK, _372_INPUT_SIZE_0.y), float2(_372_INPUT_SIZE_0.x, _372_INPUT_SIZE_0.y * _372_SHARPNESS_HACK), _372_VSCANLINES.xx);
    float2 dx = lerp(float2(1.0f / TextureSize.x, 0.0f), float2(0.0f, 1.0f / TextureSize.y), _372_VSCANLINES.xx);
    float2 dy = lerp(float2(0.0f, 1.0f / TextureSize.y), float2(1.0f / TextureSize.x, 0.0f), _372_VSCANLINES.xx);
    float2 pix_coord = (v_texCoord * TextureSize) + float2(-0.5f, 0.5f);
    float2 tc = lerp((floor(pix_coord) + 0.5f.xx) / TextureSize, (floor(pix_coord) + float2(1.0f, -0.5f)) / TextureSize, _372_VSCANLINES.xx);
    float2 fp = lerp(frac(pix_coord), frac(pix_coord.yx), _372_VSCANLINES.xx);
    float4 c00 = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, (tc - dx) - dy), float4(_372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA));
    float4 c01 = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, tc - dy), float4(_372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA));
    float4 c02 = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, (tc + dx) - dy), float4(_372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA));
    float4 c03 = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, (tc + (dx * 2.0f)) - dy), float4(_372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA));
    float4 c10 = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, tc - dx), float4(_372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA));
    float4 c11 = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, tc), float4(_372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA));
    float4 c12 = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, tc + dx), float4(_372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA));
    float4 c13 = pow(INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, tc + (dx * 2.0f)), float4(_372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA, _372_INPUT_GAMMA));
    float4x4 invX = get_hfilter_profile();
    float4x4 color_matrix0 = float4x4(float4(c00), float4(c01), float4(c02), float4(c03));
    float4x4 color_matrix1 = float4x4(float4(c10), float4(c11), float4(c12), float4(c13));
    float4 invX_Px = mul(invX, float4((fp.x * fp.x) * fp.x, fp.x * fp.x, fp.x, 1.0f));
    float4 color0 = mul(invX_Px, color_matrix0);
    float4 color1 = mul(invX_Px, color_matrix1);
    float4 min_sample0 = min(c01, c02);
    float4 max_sample0 = max(c01, c02);
    float4 min_sample1 = min(c11, c12);
    float4 max_sample1 = max(c11, c12);
    float4 aux = color0;
    color0 = clamp(color0, min_sample0, max_sample0);
    color0 = lerp(aux, color0, step(0.0f.xxxx, (c00 - c01) * (c02 - c03)) * _372_CRT_ANTI_RINGING);
    aux = color1;
    color1 = clamp(color1, min_sample1, max_sample1);
    color1 = lerp(aux, color1, step(0.0f.xxxx, (c10 - c11) * (c12 - c13)) * _372_CRT_ANTI_RINGING);
    float pos0 = fp.y;
    float pos1 = 1.0f - fp.y;
    float4 lum0 = lerp(profile.y.xxxx, profile.z.xxxx, color0);
    float4 lum1 = lerp(profile.y.xxxx, profile.z.xxxx, color1);
    float4 d0 = ((4.0f * profile.x) * pos0).xxxx / (lum0 + 1.0000000116860974230803549289703e-07f.xxxx);
    float4 d1 = ((4.0f * profile.x) * pos1).xxxx / (lum1 + 1.0000000116860974230803549289703e-07f.xxxx);
    d0 = exp((-d0) * d0);
    d1 = exp((-d1) * d1);
    float4 color = ((color0 * d0) + (color1 * d1)) * profile.w;
    float2 mask_coords = gl_FragCoord.xy - _372_OUTPUT_START_OFFSET;
    mask_coords = lerp(mask_coords, mask_coords.yx, _372_VSCANLINES.xx);
    float2 param = mask_coords;
    float param_1 = _372_MASK_INTENSITY;
    int param_2 = int(_372_PHOSPHOR_LAYOUT);
    float4 _956 = color;
    float3 _958 = _956.xyz * mask_weights(param, param_1, param_2);
    color.x = _958.x;
    color.y = _958.y;
    color.z = _958.z;
    color = clamp(pow(color, float4(1.0f / _372_OUTPUT_GAMMA, 1.0f / _372_OUTPUT_GAMMA, 1.0f / _372_OUTPUT_GAMMA, 1.0f / _372_OUTPUT_GAMMA)), 0.0f.xxxx, 1.0f.xxxx);
    FragColor = float4(color.xyz, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.y = DIRECTCRT_WINDOW_HEIGHT - gl_FragCoord.y;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    float2 directcrt_pos = float2(stage_input.gl_FragCoord.x,
        DIRECTCRT_WINDOW_HEIGHT - stage_input.gl_FragCoord.y - (1.0 / 1024.0));
    v_texCoord = (directcrt_pos - DIRECTCRT_VIEWPORT.xy) / DIRECTCRT_VIEWPORT.zw;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
