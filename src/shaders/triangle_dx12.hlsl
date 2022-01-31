cbuffer cb : register(b0)
{
    row_major float4x4 projectionMatrix : packoffset(c0);
    row_major float4x4 modelMatrix : packoffset(c4);
    row_major float4x4 viewMatrix : packoffset(c8);
};

struct VertexInput
{
    float3 inPos : POSITION;
    float3 inColor : COLOR;
};

struct VertexOutput
{
    float3 color : COLOR;
    float4 position : SV_Position;
};

VertexOutput vs_main(VertexInput vertexInput)
{
    float3 inColor = vertexInput.inColor;
    float3 inPos = vertexInput.inPos;
    float3 outColor = inColor;
    float4 position = mul(float4(inPos, 1.0f), 
                          mul(modelMatrix, mul(viewMatrix, projectionMatrix)));

    VertexOutput output;
    output.position = position;
    output.color = outColor;
    return output;
}

// Pixel
struct PixelInput
{
    float3 color : COLOR;
};

struct PixelOutput
{
    float4 attachment0 : SV_Target0;
};

PixelOutput ps_main(PixelInput pixelInput)
{
    float3 inColor = pixelInput.color;
    PixelOutput output;
    output.attachment0 = float4(inColor, 1.0f);
    return output;
}