// Generated from interpolation/sharp.glsl by gen_d3d11_shaders.py

cbuffer DirectCRT : register(b1)
{
    float4 DIRECTCRT_VIEWPORT : packoffset(c0);         // glViewport x, y, w, h
    float2 DIRECTCRT_INPUT_SIZE : packoffset(c1);       // INPUT_SIZE_0
    float DIRECTCRT_WINDOW_HEIGHT : packoffset(c1.z);
};

cbuffer Globals : register(b0)
{
    float2 _22_INPUT_SIZE_0 : packoffset(c0);
};

Texture2D<float4> INPUT_TEXTURE_0 : register(t1);
SamplerState _INPUT_TEXTURE_0_sampler : register(s1);

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

float4 texture_linear(Texture2D<float4> sampler_, SamplerState _sampler__sampler, float2 uv)
{
    float2 texCoord = (uv * _22_INPUT_SIZE_0) - 0.5f.xx;
    float2 s0t0 = floor(texCoord) + 0.5f.xx;
    float2 s0t1 = s0t0 + float2(0.0f, 1.0f);
    float2 s1t0 = s0t0 + float2(1.0f, 0.0f);
    float2 s1t1 = s0t0 + 1.0f.xx;
    float2 invTexSize = 1.0f.xx / _22_INPUT_SIZE_0;
    float4 c_s0t0 = pow(sampler_.Sample(_sampler__sampler, s0t0 * invTexSize), 2.2000000476837158203125f.xxxx);
    float4 c_s0t1 = pow(sampler_.Sample(_sampler__sampler, s0t1 * invTexSize), 2.2000000476837158203125f.xxxx);
    float4 c_s1t0 = pow(sampler_.Sample(_sampler__sampler, s1t0 * invTexSize), 2.2000000476837158203125f.xxxx);
    float4 c_s1t1 = pow(sampler_.Sample(_sampler__sampler, s1t1 * invTexSize), 2.2000000476837158203125f.xxxx);
    float2 weight = frac(texCoord);
    float4 c0 = c_s0t0 + ((c_s1t0 - c_s0t0) * weight.x);
    float4 c1 = c_s0t1 + ((c_s1t1 - c_s0t1) * weight.x);
    return c0 + ((c1 - c0) * weight.y);
}

void frag_main()
{
    float2 texel_floored = floor(v_texCoord);
    float2 s = frac(v_texCoord);
    float2 region_range = 0.5f.xx - (0.5f.xx / prescale);
    float2 center_dist = s - 0.5f.xx;
    float2 f = ((center_dist - clamp(center_dist, -region_range, region_range)) * prescale) + 0.5f.xx;
    float2 mod_texel = min(texel_floored + f, _22_INPUT_SIZE_0 - 0.5f.xx);
    float2 param = mod_texel / _22_INPUT_SIZE_0;
    FragColor = pow(texture_linear(INPUT_TEXTURE_0, _INPUT_TEXTURE_0_sampler, param), 0.4545454680919647216796875f.xxxx);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    float2 directcrt_pos = float2(stage_input.gl_FragCoord.x,
        DIRECTCRT_WINDOW_HEIGHT - stage_input.gl_FragCoord.y - (1.0 / 1024.0));
    v_texCoord = (directcrt_pos - DIRECTCRT_VIEWPORT.xy) / DIRECTCRT_VIEWPORT.zw * DIRECTCRT_INPUT_SIZE;
    prescale = stage_input.prescale;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
