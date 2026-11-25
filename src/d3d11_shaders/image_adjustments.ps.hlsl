// Generated from _internal/image-adjustments.glsl by gen_d3d11_shaders.py
// SPDX-FileCopyrightText:  2025-2026 The DOSBox Staging Team
// SPDX-License-Identifier: MIT

cbuffer Globals : register(b0)
{
    int _548_COLOR_SPACE : packoffset(c0);
    int _548_ENABLE_ADJUSTMENTS : packoffset(c0.y);
    int _548_COLOR_PROFILE : packoffset(c0.z);
    float _548_BRIGHTNESS : packoffset(c0.w);
    float _548_CONTRAST : packoffset(c1);
    float _548_GAMMA : packoffset(c1.y);
    float _548_DIGITAL_CONTRAST : packoffset(c1.z);
    float3 _548_BLACK_LEVEL_COLOR : packoffset(c2);
    float _548_BLACK_LEVEL : packoffset(c2.w);
    float _548_SATURATION : packoffset(c3);
    float _548_COLOR_TEMPERATURE_KELVIN : packoffset(c3.y);
    float _548_COLOR_TEMPERATURE_LUMA_PRESERVE : packoffset(c3.z);
    float _548_RED_GAIN : packoffset(c3.w);
    float _548_GREEN_GAIN : packoffset(c4);
    float _548_BLUE_GAIN : packoffset(c4.y);
};

Texture2D<float4> INPUT_TEXTURE_0 : register(t1);
SamplerState _INPUT_TEXTURE_0_sampler : register(s1);

static float2 v_texCoord;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 v_texCoord : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float3 plant(float3 tar, float r)
{
    float t = max(max(tar.x, tar.y), tar.z) + 9.9999997473787516355514526367188e-06f;
    return (tar * r) / t.xxx;
}

float3 sigmoid_contrast(float3 color, float amount)
{
    float x = max(max(color.x, color.y), color.z);
    float c = max(lerp(x, smoothstep(0.0f, 1.0f, x), amount), 0.0f);
    float3 param = color;
    float param_1 = c;
    return plant(param, param_1);
}

float luminance(float3 x)
{
    return dot(x, float3(0.21265600621700286865234375f, 0.715157985687255859375f, 0.072186000645160675048828125f));
}

float3 saturation(float3 color, float s)
{
    float3 param = color;
    return clamp(lerp(luminance(param).xxx, color, (s + 1.0f).xxx), 0.0f.xxx, 1.0f.xxx);
}

float3 rgb_to_hcv(float3 color)
{
    float4 _143;
    if (color.y < color.z)
    {
        _143 = float4(color.zy, -1.0f, 0.666666686534881591796875f);
    }
    else
    {
        _143 = float4(color.yz, 0.0f, -0.3333333432674407958984375f);
    }
    float4 p = _143;
    float4 _168;
    if (color.x < p.x)
    {
        _168 = float4(p.xyw, color.x);
    }
    else
    {
        _168 = float4(color.x, p.yzx);
    }
    float4 q1 = _168;
    float c = q1.x - min(q1.w, q1.y);
    float h = abs(((q1.w - q1.y) / ((6.0f * c) + 9.9999999747524270787835121154785e-07f)) + q1.z);
    return float3(h, c, q1.x);
}

float3 rgb_to_hsl(inout float3 color)
{
    color = max(color, 9.9999999747524270787835121154785e-07f.xxx);
    float3 param = color;
    float3 hcv = rgb_to_hcv(param);
    float l = hcv.z - (hcv.y * 0.5f);
    float s = hcv.y / ((1.0f - abs((l * 2.0f) - 1.0f)) + 9.9999999747524270787835121154785e-07f);
    return float3(hcv.x, s, l);
}

float3 kelvin_to_rgb(float k)
{
    float kelvin = clamp(k, 1000.0f, 40000.0f) / 100.0f;
    float3 ret;
    if (kelvin <= 66.0f)
    {
        ret.x = 1.0f;
        ret.y = clamp((0.390081584453582763671875f * log(kelvin)) - 0.6318414211273193359375f, 0.0f, 1.0f);
    }
    else
    {
        float t = max(kelvin - 60.0f, 0.0f);
        ret.x = clamp(1.29293620586395263671875f * pow(t, -0.133204758167266845703125f), 0.0f, 1.0f);
        ret.y = clamp(1.129890918731689453125f * pow(t, -0.075514845550060272216796875f), 0.0f, 1.0f);
    }
    if (kelvin >= 66.0f)
    {
        ret.z = 1.0f;
    }
    else
    {
        if (kelvin < 19.0f)
        {
            ret.z = 0.0f;
        }
        else
        {
            ret.z = clamp((0.54320681095123291015625f * log(kelvin - 10.0f)) - 1.19625413417816162109375f, 0.0f, 1.0f);
        }
    }
    return ret;
}

float3 hue_to_rgb(float hue)
{
    return clamp(float3(abs((hue * 6.0f) - 3.0f) - 1.0f, 2.0f - abs((hue * 6.0f) - 2.0f), 2.0f - abs((hue * 6.0f) - 4.0f)), 0.0f.xxx, 1.0f.xxx);
}

float3 hsl_to_rgb(float3 hsl)
{
    float param = hsl.x;
    float3 color = hue_to_rgb(param);
    float c = (1.0f - abs((2.0f * hsl.z) - 1.0f)) * hsl.y;
    return ((color - 0.5f.xxx) * c) + hsl.z.xxx;
}

float3 color_temperature(inout float3 color, float kelvin, float luma_preserve)
{
    float3 param = color;
    float3 _375 = rgb_to_hsl(param);
    float orig_luma = _375.z;
    float param_1 = kelvin;
    color *= kelvin_to_rgb(param_1);
    float3 param_2 = color;
    float3 _385 = rgb_to_hsl(param_2);
    float3 param_3 = float3(_385.xy, orig_luma);
    float3 color2 = hsl_to_rgb(param_3);
    return lerp(color, color2, luma_preserve.xxx);
}

float eotf_1886a(inout float color, float bl, float brightness, float contrast)
{
    float b = pow(bl, 0.4166666567325592041015625f);
    float a = 6.81292057037353515625f - b;
    b = ((brightness - 50.0f) / 250.0f) + (b / a);
    float _419;
    if (contrast != 50.0f)
    {
        _419 = pow(2.0f, (contrast - 50.0f) / 50.0f);
    }
    else
    {
        _419 = 1.0f;
    }
    a = _419;
    float Lw = 1.0f * a;
    float Lb = min(b * a, 0.3499999940395355224609375f);
    float k = Lw / pow(1.0f + Lb, 2.599999904632568359375f);
    float sl = k * pow(0.3499999940395355224609375f + Lb, -0.4000000059604644775390625f);
    float _453;
    if (color >= 0.3499999940395355224609375f)
    {
        _453 = k * pow(color + Lb, 2.599999904632568359375f);
    }
    else
    {
        _453 = sl * pow(color + Lb, 3.0f);
    }
    color = _453;
    float bc = 0.00446395017206668853759765625f * pow(bl, 1.234859943389892578125f);
    color = min(max(color - bc, 0.0f) * (1.0f / (1.0f - bc)), 1.0f);
    color = pow(color, 1.0f - (0.00843282975256443023681640625f * pow(bl, 1.22743999958038330078125f)));
    return color;
}

float3 eotf_1886a(inout float3 color, float black_level, float brightness, float contrast)
{
    float param = color.x;
    float param_1 = black_level;
    float param_2 = brightness;
    float param_3 = contrast;
    float _505 = eotf_1886a(param, param_1, param_2, param_3);
    color.x = _505;
    float param_4 = color.y;
    float param_5 = black_level;
    float param_6 = brightness;
    float param_7 = contrast;
    float _516 = eotf_1886a(param_4, param_5, param_6, param_7);
    color.y = _516;
    float param_8 = color.z;
    float param_9 = black_level;
    float param_10 = brightness;
    float param_11 = contrast;
    float _527 = eotf_1886a(param_8, param_9, param_10, param_11);
    color.z = _527;
    return color;
}

void frag_main()
{
    float3 color = INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, v_texCoord).xyz;
    float3 orig_color = color;
    float3 param = color;
    float param_1 = _548_DIGITAL_CONTRAST;
    color = sigmoid_contrast(param, param_1);
    color = max(color, _548_BLACK_LEVEL_COLOR * _548_BLACK_LEVEL);
    float3 param_2 = color;
    float param_3 = _548_SATURATION;
    color = saturation(param_2, param_3);
    float3 param_4 = color;
    float param_5 = _548_COLOR_TEMPERATURE_KELVIN;
    float param_6 = _548_COLOR_TEMPERATURE_LUMA_PRESERVE;
    float3 _584 = color_temperature(param_4, param_5, param_6);
    color = _584;
    if (_548_ENABLE_ADJUSTMENTS == 0)
    {
        color = orig_color;
    }
    float out_gamma = 0.0f;
    float3x3 XYZ_to_linear_RGB = float3x3(0.0f.xxx, 0.0f.xxx, 0.0f.xxx);
    if (_548_COLOR_SPACE == 1)
    {
        out_gamma = 2.599999904632568359375f;
        XYZ_to_linear_RGB = float3x3(float3(2.7253940105438232421875f, -0.795167982578277587890625f, 0.0412419997155666351318359375f), float3(-1.018002986907958984375f, 1.68973195552825927734375f, -0.087638996541500091552734375f), float3(-0.4401629865169525146484375f, 0.022647000849246978759765625f, 1.1009290218353271484375f));
    }
    else
    {
        if (_548_COLOR_SPACE == 2)
        {
            out_gamma = 2.599999904632568359375f;
            XYZ_to_linear_RGB = float3x3(float3(2.49349689483642578125f, -0.8294889926910400390625f, 0.0358459986746311187744140625f), float3(-0.931384027004241943359375f, 1.76266396045684814453125f, -0.076172001659870147705078125f), float3(-0.4027110040187835693359375f, 0.02362499944865703582763671875f, 0.95688498020172119140625f));
        }
        else
        {
            if (_548_COLOR_SPACE == 3)
            {
                out_gamma = 2.2000000476837158203125f;
                XYZ_to_linear_RGB = float3x3(float3(2.49349689483642578125f, -0.8294889926910400390625f, 0.0358459986746311187744140625f), float3(-0.931384027004241943359375f, 1.76266396045684814453125f, -0.076172001659870147705078125f), float3(-0.4027110040187835693359375f, 0.02362499944865703582763671875f, 0.95688498020172119140625f));
            }
            else
            {
                if (_548_COLOR_SPACE == 4)
                {
                    out_gamma = 2.2000000476837158203125f;
                    XYZ_to_linear_RGB = float3x3(float3(2.7917230129241943359375f, -0.89476597309112548828125f, 0.0416780002415180206298828125f), float3(-1.17316496372222900390625f, 1.81558597087860107421875f, -0.13088600337505340576171875f), float3(-0.4409730136394500732421875f, 0.0320000015199184417724609375f, 1.0020339488983154296875f));
                }
                else
                {
                    if (_548_COLOR_SPACE == 5)
                    {
                        out_gamma = 2.2000000476837158203125f;
                        XYZ_to_linear_RGB = float3x3(float3(2.0415880680084228515625f, -0.9692440032958984375f, 0.013443999923765659332275390625f), float3(-0.5650069713592529296875f, 1.87596797943115234375f, -0.11835999786853790283203125f), float3(-0.3447310030460357666015625f, 0.0415549986064434051513671875f, 1.01517498493194580078125f));
                    }
                    else
                    {
                        if (_548_COLOR_SPACE == 6)
                        {
                            out_gamma = 2.400000095367431640625f;
                            XYZ_to_linear_RGB = float3x3(float3(1.71665096282958984375f, -0.666683971881866455078125f, 0.01764000020921230316162109375f), float3(-0.355670988559722900390625f, 1.61648094654083251953125f, -0.0427710004150867462158203125f), float3(-0.253365993499755859375f, 0.01576899923384189605712890625f, 0.94210302829742431640625f));
                        }
                        else
                        {
                            out_gamma = 2.2000000476837158203125f;
                            XYZ_to_linear_RGB = float3x3(float3(3.2409698963165283203125f, -0.9692440032958984375f, 0.0556299984455108642578125f), float3(-1.53738296031951904296875f, 1.87596797943115234375f, -0.20397700369358062744140625f), float3(-0.4986110031604766845703125f, 0.0415549986064434051513671875f, 1.056972026824951171875f));
                        }
                    }
                }
            }
        }
    }
    float3x3 sRGB_to_XYZ = float3x3(float3(0.412391006946563720703125f, 0.2126390039920806884765625f, 0.019331000745296478271484375f), float3(0.3575839996337890625f, 0.715169012546539306640625f, 0.11919499933719635009765625f), float3(0.18048100173473358154296875f, 0.07219199836254119873046875f, 0.950532019138336181640625f));
    if (_548_COLOR_PROFILE == 1)
    {
        sRGB_to_XYZ = float3x3(float3(0.4305540025234222412109375f, 0.222003996372222900390625f, 0.02018200047314167022705078125f), float3(0.34154999256134033203125f, 0.706655025482177734375f, 0.12955300509929656982421875f), float3(0.17835199832916259765625f, 0.071341000497341156005859375f, 0.939321994781494140625f));
    }
    else
    {
        if (_548_COLOR_PROFILE == 2)
        {
            sRGB_to_XYZ = float3x3(float3(0.3966859877109527587890625f, 0.21029900014400482177734375f, 0.006130999885499477386474609375f), float3(0.3725039958953857421875f, 0.71376597881317138671875f, 0.1153559982776641845703125f), float3(0.18126599490642547607421875f, 0.075935997068881988525390625f, 0.9675710201263427734375f));
        }
        else
        {
            if (_548_COLOR_PROFILE == 3)
            {
                sRGB_to_XYZ = float3x3(float3(0.393521010875701904296875f, 0.21237599849700927734375f, 0.01873899996280670166015625f), float3(0.3652580082416534423828125f, 0.701059997081756591796875f, 0.111933998763561248779296875f), float3(0.1916770040988922119140625f, 0.086563996970653533935546875f, 0.958384990692138671875f));
            }
            else
            {
                if (_548_COLOR_PROFILE == 4)
                {
                    sRGB_to_XYZ = float3x3(float3(0.392257988452911376953125f, 0.20940999686717987060546875f, 0.016061000525951385498046875f), float3(0.3511349856853485107421875f, 0.72567999362945556640625f, 0.093635998666286468505859375f), float3(0.1666029989719390869140625f, 0.064910002052783966064453125f, 0.850323975086212158203125f));
                }
                else
                {
                    if (_548_COLOR_PROFILE == 5)
                    {
                        sRGB_to_XYZ = float3x3(float3(0.37792301177978515625f, 0.19567899405956268310546875f, 0.01051400043070316314697265625f), float3(0.31736600399017333984375f, 0.72231900691986083984375f, 0.097825996577739715576171875f), float3(0.20773799717426300048828125f, 0.08200199902057647705078125f, 1.07695996761322021484375f));
                    }
                    else
                    {
                        sRGB_to_XYZ = float3x3(float3(0.412391006946563720703125f, 0.2126390039920806884765625f, 0.019331000745296478271484375f), float3(0.3575839996337890625f, 0.715169012546539306640625f, 0.11919499933719635009765625f), float3(0.18048100173473358154296875f, 0.07219199836254119873046875f, 0.950532019138336181640625f));
                    }
                }
            }
        }
    }
    if (_548_ENABLE_ADJUSTMENTS == 0)
    {
        sRGB_to_XYZ = float3x3(float3(0.412391006946563720703125f, 0.2126390039920806884765625f, 0.019331000745296478271484375f), float3(0.3575839996337890625f, 0.715169012546539306640625f, 0.11919499933719635009765625f), float3(0.18048100173473358154296875f, 0.07219199836254119873046875f, 0.950532019138336181640625f));
    }
    color = pow(color, 2.2000000476837158203125f.xxx);
    color = mul(color, sRGB_to_XYZ);
    color = mul(color, XYZ_to_linear_RGB);
    orig_color = color;
    color *= float3(_548_RED_GAIN * _548_RED_GAIN, _548_GREEN_GAIN * _548_GREEN_GAIN, _548_BLUE_GAIN * _548_BLUE_GAIN);
    if (_548_ENABLE_ADJUSTMENTS == 0)
    {
        color = orig_color;
    }
    color = pow(color, 0.4545454680919647216796875f.xxx);
    orig_color = color;
    float3 param_7 = color;
    float param_8 = 0.0381640456616878509521484375f;
    float param_9 = _548_BRIGHTNESS;
    float param_10 = _548_CONTRAST;
    float3 _883 = eotf_1886a(param_7, param_8, param_9, param_10);
    color = _883;
    color = pow(color, (1.0f / (out_gamma + _548_GAMMA)).xxx);
    if (_548_ENABLE_ADJUSTMENTS == 0)
    {
        color = orig_color;
    }
    color = clamp(color, 0.0f.xxx, 1.0f.xxx);
    FragColor = float4(color, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    v_texCoord = stage_input.v_texCoord;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
