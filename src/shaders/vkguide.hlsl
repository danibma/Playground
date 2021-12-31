struct VSInput {
	[[vk::location(0)]] float3 vPosition : POSITION;
	[[vk::location(1)]] float3 vNormal : NORMAL;
	[[vk::location(2)]] float3 vColor : COLOR;
	[[vk::location(3)]] float3 vTexCoord : TEXCOORD1;

	uint vertexID : SV_VertexID;
	uint instanceID : SV_InstanceID;
};

struct VSOutput {
	float4 position : SV_POSITION;

	[[vk::location(0)]] float3 outColor;
	[[vk::location(1)]] float3 outTexCoord;
};

struct CameraBuffer {
	matrix view;
	matrix projection;
	matrix viewproj;
};
[[vk::binding(0, 0)]] ConstantBuffer<CameraBuffer> cameraData;

struct ObjectData {
	matrix model;
};

struct ObjectBuffer {
	ObjectData objects[];
};
[[vk::binding(1, 0)]] ConstantBuffer<ObjectBuffer> objectBuffer;

[[vk::push_constant]]
cbuffer PushConstant {
	float4 data;
	matrix renderMatrix;
};

VSOutput vs_main(VSInput input) {
	VSOutput output;

	matrix modelMatrix = objectBuffer.objects[input.instanceID].model;
	matrix transformationMatrix = (cameraData.viewproj * modelMatrix);
	output.position = transformationMatrix * float4(input.vPosition, 1.0f);
	output.outColor = input.vColor;
	output.outTexCoord = input.vTexCoord;

	return output;
}

// ------------ Pixel Shader
[[vk::binding(2, 0)]] Texture2D<float4> tex1;

struct SceneData {
	float4 fogColor; // w is for exponent
	float4 fogDistances; //x for min, y for max, zw unused.
	float4 ambientColor;
	float4 sunlightDirection; //w for sun power
	float4 sunlightColor;
};
[[vk::binding(0, 1)]] ConstantBuffer<SceneData> sceneData;

float4 ps_main(VSOutput output) : SV_TARGET
{
	// vec3 color = texture(tex1, inTexCoord).xyz;
	float3 color = output.outColor;
	float4 outFragColor = float4(color, 1.0f);

	return outFragColor;
}