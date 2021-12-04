struct vs_input
{
    uint vertexID : SV_VertexID;
};

struct vs_output
{
    float4 pos : SV_POSITION;
};

static const float3 vertices[] =
{
    float3(-0.5, 0.5, 0),
	float3(0.5, -0.5, 0),
	float3(-0.5, -0.5, 0),
};

vs_output vs_main(vs_input input)
{
    vs_output output;
    output.pos = float4(vertices[input.vertexID], 1.0f);
    return output;
}

float4 ps_main(vs_output output) : SV_TARGET
{
    return float4(1.0f, 0.5f, 1.0f, 1.0f);
}