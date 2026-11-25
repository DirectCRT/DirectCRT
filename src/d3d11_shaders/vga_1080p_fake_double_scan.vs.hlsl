// Generated from crt/vga-1080p-fake-double-scan.glsl by gen_d3d11_shaders.py
// SPDX-FileCopyrightText:  2020-2026 The DOSBox Staging Team
// SPDX-License-Identifier: MIT

cbuffer Globals : register(b0)
{
    float2 _43_INPUT_SIZE_0 : packoffset(c0);
    float2 _43_OUTPUT_SIZE : packoffset(c0.z);
};


static float4 gl_Position;
static float2 a_position;
static float2 v_texCoord;
static float2 prescale;

struct SPIRV_Cross_Input
{
    float2 a_position : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float2 v_texCoord : TEXCOORD0;
    float2 prescale : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position = float4(a_position, 0.0f, 1.0f);
    v_texCoord = (float2(a_position.x + 1.0f, a_position.y + 1.0f) / 2.0f.xx) * _43_INPUT_SIZE_0;
    prescale = ceil(_43_OUTPUT_SIZE / _43_INPUT_SIZE_0);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    a_position = stage_input.a_position;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.v_texCoord = v_texCoord;
    stage_output.prescale = prescale;
    return stage_output;
}
