cbuffer constants : register(b0)
{
    float4x4 MVPMatrix;
}

struct vs_input
{
    float3 pos : POSITION;
    float2 tex : TEX;
    //float3 color : COLOR;
    float3 normal : NORMAL;
};

struct vs_output
{
    float4 pos : SV_POSITION;
    float2 tex : TEX;
    //float4 color : COLOR;
    float3 normal : NORMAL;
    float3 inWorldPos : WORLD_POSITION;
};

Texture2D tex : register(t0);
SamplerState samplerState : register(s0);

static float4x4 Identity =
{
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 0, 1 }
};

vs_output vs_main(vs_input input)
{
    vs_output output;
    output.pos = mul(float4(input.pos, 1.0f), MVPMatrix);
    //output.pos = mul(float4(input.pos, 1.0f), 1.0f);
    output.tex = input.tex;
    output.normal = input.normal;
    output.inWorldPos = mul(float4(input.pos, 1.0f), Identity);

    return output;
}

float4 ps_main( vs_output output ) : SV_TARGET
{
    //float4 sampleColor = tex.Sample(samplerState, output.tex);
    float4 sampleColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

    // Ambient Light
    float ambientLightStrength = 0.6f;
    float4 ambientLightColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 ambientLight = ambientLightStrength * ambientLightColor;

    // Diffuse Light
    float diffuseLightStrength = 1.0f;
    float4 diffuseLightColor = float4(0.0f, 0.5f, 1.0f, 1.0f);
    float3 diffuseLightLocation = float3(0.0f, 0.0f, -2.0f);
    float3 norm = normalize(output.normal);
    float3 lightDir = normalize(diffuseLightLocation - output.inWorldPos);
    float4 diffuseLight = saturate(dot(norm, lightDir)) * diffuseLightColor * diffuseLightStrength;


    return (diffuseLight + ambientLight) * sampleColor;
}