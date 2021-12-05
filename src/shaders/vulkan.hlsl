struct vs_input
{
    uint vertexID : SV_VertexID;
};

struct vs_output
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

static const float2 vertices[] =
{
    float2(0.0, -0.5),
    float2(0.5, 0.5),
    float2(-0.5, 0.5)
};

static const float3 colors[3] =
{
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0)
};

vs_output vs_main(vs_input input)
{
    vs_output output;
    output.pos = float4(vertices[input.vertexID], 0.0f, 1.0f);
    return output;
}

float4 ps_main(vs_output output) : SV_TARGET
{
    return output.color;
}