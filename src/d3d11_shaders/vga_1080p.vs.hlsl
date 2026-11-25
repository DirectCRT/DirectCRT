// Generated from crt/vga-1080p.glsl by gen_d3d11_shaders.py
// SPDX-FileCopyrightText:  2020-2026 The DOSBox Staging Team
// SPDX-License-Identifier: MIT

cbuffer Globals : register(b0)
{
    float2 _44_INPUT_SIZE_0 : packoffset(c0);
};


static float4 gl_Position;
static float2 a_position;
static float2 v_texCoord;
static float4 sourceSize;
static float2 onex;
static float2 oney;

struct SPIRV_Cross_Input
{
    float2 a_position : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float2 v_texCoord : TEXCOORD0;
    float4 sourceSize : TEXCOORD1;
    float2 onex : TEXCOORD2;
    float2 oney : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position = float4(a_position, 0.0f, 1.0f);
    v_texCoord = float2(a_position.x + 1.0f, a_position.y + 1.0f) / 2.0f.xx;
    sourceSize = float4(_44_INPUT_SIZE_0, 1.0f.xx / _44_INPUT_SIZE_0);
    onex = float2(sourceSize.z, 0.0f);
    oney = float2(0.0f, sourceSize.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    a_position = stage_input.a_position;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.v_texCoord = v_texCoord;
    stage_output.sourceSize = sourceSize;
    stage_output.onex = onex;
    stage_output.oney = oney;
    return stage_output;
}
