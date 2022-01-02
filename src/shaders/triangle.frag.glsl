#version 460
//! #extension GL_KHR_vulkan_glsl : enable

layout (push_constant) uniform MaterialConstant
{
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	vec4 shininess;
} Material;

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inFragPos;
layout (location = 4) in vec3 inViewPos;

layout (location = 0) out vec4 outFragColor;

layout(set = 2, binding = 0) uniform sampler2D tex1;

layout(set = 0, binding = 1) uniform  SceneData{   
    vec4 fogColor; // w is for exponent
	vec4 fogDistances; //x for min, y for max, zw unused.
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
} sceneData;

/*layout(set = 3, binding = 0) uniform MaterialBuffer {
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	vec4 shininess;
} Material;*/

void main()
{
	vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);
	vec3 lightPos = vec3(0, -10, 0);
	vec3 normal = normalize(inNormal);
	vec3 lightDir = normalize(lightPos - inFragPos);

	// Ambient Light
	float ambientStrength = 0.1f;
	vec3 ambientLight = lightColor * Material.ambient;

	// Diffuse Light
	float diff = max(dot(normal, lightDir), 0.0f);
	vec3 diffuse = lightColor * (diff * Material.diffuse);

	// Specular Light
	float specularStrength = 0.5f;
	vec3 viewDir = normalize(inViewPos - inFragPos);
	vec3 reflectDir = reflect(-lightDir, normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0f), Material.shininess.x);
	vec3 specular = lightColor * (spec * Material.specular);

	//vec3 color = texture(tex1, inTexCoord).xyz;
	vec3 color = (ambientLight + diffuse + specular);
	outFragColor = vec4(color, 1.0f);
}