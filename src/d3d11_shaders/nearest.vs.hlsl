// Generated from interpolation/nearest.glsl by gen_d3d11_shaders.py

static float4 gl_Position;
static float2 a_position;
static float2 v_texCoord;

struct SPIRV_Cross_Input
{
    float2 a_position : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float2 v_texCoord : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position = float4(a_position, 0.0f, 1.0f);
    v_texCoord = float2(a_position.x + 1.0f, a_position.y + 1.0f) / 2.0f.xx;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    a_position = stage_input.a_position;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.v_texCoord = v_texCoord;
    return stage_output;
}
