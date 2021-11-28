cbuffer constants : register(b0)
{
    float4x4 MVPMatrix;
}

struct vs_input
{
    float3 pos : POSITION;
    float2 tex : TEX;
    float3 color : COLOR;
};

struct vs_output
{
    float4 pos : SV_POSITION;
    float2 tex : TEX;
    float4 color : COLOR;
};

Texture2D tex : register(t0);
SamplerState samplerState : register(s0);

vs_output vs_main(vs_input input)
{
    vs_output output;
    output.pos = mul(float4(input.pos, 1.0f), MVPMatrix);
    //output.pos = mul(float4(input.pos, 1.0f), 1.0f);
    output.color = float4(input.color, 1.0f);
    output.tex = input.tex;

    return output;
}

float4 ps_main( vs_output output ) : SV_TARGET
{
    float4 sampleColor = tex.Sample(samplerState, output.tex);

    // Ambient Light
    float ambientLightStrength = 0.6f;
    float4 ambientLightColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 ambientLight = ambientLightStrength * ambientLightColor;

    return sampleColor * ambientLight;
}