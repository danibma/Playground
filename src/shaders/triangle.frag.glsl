#version 460
//! #extension GL_KHR_vulkan_glsl : enable

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inFragPos;
layout (location = 4) in vec3 inViewPos;

layout (location = 0) out vec4 outFragColor;

layout(set = 2, binding = 0) uniform sampler2D diffuseMap;
layout(set = 2, binding = 1) uniform sampler2D specularMap;
layout(set = 2, binding = 2) uniform sampler2D emissionMap;

layout(set = 0, binding = 1) uniform  SceneData{   
    vec4 fogColor; // w is for exponent
	vec4 fogDistances; //x for min, y for max, zw unused.
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
} sceneData;

layout(set = 3, binding = 0) uniform MaterialBuffer {
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	vec4 shininess;
} Material;

layout(set = 3, binding = 1) uniform Light {
	vec3 position;
  
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

	vec3 attenuation; // x = constant, y = linear, z = quadratic

} light;

void main()
{
	vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);
	vec3 normal = normalize(inNormal);
	vec3 lightDir = normalize(light.position - inFragPos);

	// Ambient
	float ambientStrength = 0.1f;
	vec3 ambientLight = light.ambient * vec3(texture(diffuseMap, inTexCoord));

	// Diffuse
	float diff = max(dot(normal, lightDir), 0.0f);
	vec3 diffuse = light.diffuse * diff * vec3(texture(diffuseMap, inTexCoord));

	// Specular
	float specularStrength = 0.5f;
	vec3 viewDir = normalize(inViewPos - inFragPos);
	vec3 reflectDir = reflect(-lightDir, normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0f), Material.shininess.x);
	vec3 specular = light.specular * spec * vec3(texture(specularMap, inTexCoord));

	// Emissive
	//vec3 emission = texture(emissionMap, inTexCoord).rgb;
	vec3 emission = vec3(0.0f);

	// Attenuation
	float distance    = length(light.position - inFragPos);
	float attenuation = 1.0 / 
						(light.attenuation.x 
						+ light.attenuation.y * distance 
						+ light.attenuation.z * (distance * distance));    

	ambientLight *= attenuation;
	diffuse *= attenuation;
	specular *= attenuation;

	vec3 color = (ambientLight + diffuse + specular + emission);
	outFragColor = vec4(color, 1.0f);
}