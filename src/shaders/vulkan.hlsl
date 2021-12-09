struct vs_input
{
    [[vk::location(0)]] float2 inPos : POSITION;
    [[vk::location(1)]] float3 inColor : COLOR;
};

[[vk::push_constant]]
cbuffer UniformBufferObject
{
    matrix mvp;
};

struct vs_output
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

vs_output vs_main(vs_input input)
{
    vs_output output;
    output.pos = mul(float4(input.inPos, 0.0f, 1.0f), mvp);
    output.color = float4(input.inColor, 1.0f);
    return output;
}

float4 ps_main(vs_output output) : SV_TARGET
{
    return output.color;
}