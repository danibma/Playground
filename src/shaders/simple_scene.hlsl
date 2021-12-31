cbuffer constants : register(b0)
{
    matrix MVPMatrix;
    matrix WorldMatrix;
    float3 viewerPosition;
    float3 lightLocation;
}

struct vs_input
{
    float3 pos : POSITION;
    float2 tex : TEX;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct vs_output
{
    float4 pos : SV_POSITION;
    float3 color : COLOR;
    float2 tex : TEX;
    float3 normal : NORMAL;
    float3 inWorldPos : WORLD_POSITION;
};

Texture2D tex : register(t0);
SamplerState samplerState : register(s0);

vs_output vs_main(vs_input input)
{
    vs_output output;
    output.pos = mul(float4(input.pos, 1.0f), MVPMatrix);
    output.color = input.color;
    //output.pos = mul(float4(input.pos, 1.0f), 1.0f);
    output.tex = input.tex;
    output.normal = input.normal;
    output.inWorldPos = mul(float4(input.pos, 1.0f), WorldMatrix);

    return output;
}

float4 ps_main( vs_output output ) : SV_TARGET
{
    // TODO: Do Lighting Calculations in view space
    float4 sampleColor = tex.Sample(samplerState, output.tex);
    //float4 sampleColor = float4(output.color, 1.0f);
    
    // Light
    float lightColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    //float3 lightLocation = float3(0.0f, 1.0f, -2.0f);
    float3 lightDir = normalize(lightLocation - output.inWorldPos);

    // Ambient Light
    float ambientLightStrength = 0.5f;
    float4 ambientLight = ambientLightStrength * lightColor;

    // Diffuse Light
    float3 norm = normalize(output.normal);
    float4 diffuseLight = saturate(dot(norm, lightDir)) * lightColor;

    // Specular Light
    float specularStrength = 0.5f;
    float3 viewDir = normalize(viewerPosition - output.inWorldPos);
    float3 reflectDir = reflect(-lightDir, norm);
    float4 specularLight = specularStrength * pow(saturate(dot(viewDir, reflectDir)), 32) * lightColor;

    return (ambientLight + diffuseLight) * sampleColor;
}