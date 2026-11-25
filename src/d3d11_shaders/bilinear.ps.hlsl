// Generated from interpolation/bilinear.glsl by gen_d3d11_shaders.py
// SPDX-FileCopyrightText:  2020-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2020-2020 Hyllian <sergiogdb@gmail.com>
// SPDX-FileCopyrightText:  2020-2020 jmarsh <jmarsh@vogons.org>
// SPDX-License-Identifier: GPL-2.0-or-later

cbuffer DirectCRT : register(b1)
{
    float4 DIRECTCRT_VIEWPORT : packoffset(c0);         // glViewport x, y, w, h
    float2 DIRECTCRT_INPUT_SIZE : packoffset(c1);       // INPUT_SIZE_0
    float DIRECTCRT_WINDOW_HEIGHT : packoffset(c1.z);
};

Texture2D<float4> INPUT_TEXTURE_0 : register(t0);
SamplerState _INPUT_TEXTURE_0_sampler : register(s0);

static float4 FragColor;
static float2 v_texCoord;

struct SPIRV_Cross_Input
{
    float2 v_texCoord : TEXCOORD0;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

void frag_main()
{
    FragColor = INPUT_TEXTURE_0.Sample(_INPUT_TEXTURE_0_sampler, v_texCoord);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    float2 directcrt_pos = float2(stage_input.gl_FragCoord.x,
        DIRECTCRT_WINDOW_HEIGHT - stage_input.gl_FragCoord.y - (1.0 / 1024.0));
    v_texCoord = (directcrt_pos - DIRECTCRT_VIEWPORT.xy) / DIRECTCRT_VIEWPORT.zw;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
