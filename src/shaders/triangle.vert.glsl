#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec3 vTexCoord;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec3 outTexCoord;

layout(set = 0, binding = 0) uniform CameraBuffer {
	mat4 view;
	mat4 projection;
	mat4 viewproj;
} cameraData;

struct ObjectData{
	mat4 model;
};
//all object matrices
layout(std140,set = 1, binding = 0) readonly buffer ObjectBuffer{
	ObjectData objects[];
} objectBuffer;

layout (push_constant) uniform constants 
{
	vec4 data;
	mat4 renderMatrix;
} PushConstants;

void main() {
	/*
	 * We are using gl_BaseInstance to access the object buffer. 
	 * This is due to how Vulkan works on its normal draw calls. 
	 * All the draw commands in Vulkan request “first Instance” 
	 * and “instance count”. We are not doing instanced rendering, 
	 * so instance count is always 1. But we can still change the 
	 * “first instance” parameter, and this way get gl_BaseInstance 
	 * as a integer we can use for whatever use we want to in the shader. 
	 * This gives us a simple way to send a single integer to the shader 
	 * without setting up push constants or descriptors.
	*/
	mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].model;
	mat4 transformationMatrix = (cameraData.viewproj * modelMatrix);
	gl_Position = transformationMatrix * vec4(vPosition, 1.0f);
	outColor = vColor;
	outTexCoord = vTexCoord;
}