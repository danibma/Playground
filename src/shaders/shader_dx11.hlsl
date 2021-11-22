cbuffer constants : register(b0)
{
    float4x4 modelViewProj;
}

struct VS_Input
{
	float3 pos : POSITION;
	float2 tex : TEX;
	float4 color : COLOR;
};

struct VS_Output
{
	float4 position : SV_POSITION;
	float2 tex : TEX;
	float4 color : COLOR;
};

Texture2D tex : register(t0);
SamplerState samplerState : register(s0);

VS_Output vs_main(VS_Input input)
{
	VS_Output output;

    output.position = mul(modelViewProj, float4(input.pos, 1.0f));
	output.color = input.color;
	output.tex = input.tex;

	return output;
}

float4 ps_main(VS_Output input) : SV_TARGET
{
	//return tex.Sample(samplerState, input.tex) * input.color;
	return tex.Sample(samplerState, input.tex) * float4(1, 1, 1, 1);
}