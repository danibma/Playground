#if 1

#include <assert.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>

#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>

#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <array>
#include <chrono>
#include <functional>
#include <sstream>

#include <vkBoostrap/VkBootstrap.h>

#include <tinyobjloader/tiny_obj_loader.h>

#define VMA_IMPLEMENTATION
#include "VulkanMemoryAllocator/vk_mem_alloc.h"

#include "helper.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NOEXCEPTION // optional. disable exception handling.
#define TINYGLTF_USE_CPP14
#include "tiny_gltf.h"

//#define IMGUI_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define IMGUI_VULKAN_DEBUG_REPORT
#endif

constexpr int width = 1600;
constexpr int height = 900;

constexpr uint32_t frame_overlap = 2;

#define vkCheck(x)														\
		{ VkResult err = x;												\
		if (err)														\
		{																\
			std::cout <<"Detected Vulkan error: " << err << std::endl;	\
			__debugbreak();													\
		}}				

struct VertexInputDescription {

	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct AllocatedBuffer
{
	VkBuffer buffer;
	VmaAllocation allocation;
};

struct AllocatedImage {
	VkImage image;
	VmaAllocation allocation;
};

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 uv;
	static VertexInputDescription GetVertexDescription();
};

struct Mesh
{
	std::vector<Vertex> vertices;
	AllocatedBuffer vertexBuffer;

	bool loadFromObj(const char* file, const char* material_path);
	bool loadFromGLTF(const char* file);
};

struct MeshPushConstants
{
	glm::vec4 data;
	glm::mat4 renderMatrix;
};

struct Material
{
	glm::vec4 ambient;
	glm::vec4 diffuse;
	glm::vec4 specular;
	glm::vec4 shininess;
};

struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 viewproj;
	glm::vec4 position;
};

struct GPUSceneData {
	glm::vec4 fogColor; // w is for exponent
	glm::vec4 fogDistances; //x for min, y for max, zw unused.
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; //w for sun power
	glm::vec4 sunlightColor;
};

struct GPUObjectData {
	glm::mat4 modelMatrix;
};

struct FrameData {
	VkSemaphore presentSemaphore, renderSemaphore;
	VkFence renderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptorSet;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptorSet;
};

struct UploadContext {
	VkFence uploadFence;
	VkCommandPool commandPool;
};

struct Texture {
	AllocatedImage image;
	VkImageView imageView;
};

ImDrawData* draw_data;

VkPhysicalDeviceProperties gpuProperties;
VkInstance instance; // Vulkan library handle
VkDebugUtilsMessengerEXT debugMessenger; // Vulkan debug output handle
VkPhysicalDevice chosenGPU; // GPU chosen as the default device
VkDevice device; // Vulkan device for commands
VkSurfaceKHR surface; // Vulkan window surface
VkSwapchainKHR swapchain;
VkFormat swapchainImageFormat;
std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
VkQueue graphicsQueue; //queue we will submit to
uint32_t graphicsQueueFamily; //family of that queue
VkRenderPass renderPass;
std::vector<VkFramebuffer> framebuffers;
uint32_t frameNumber;
std::vector<VkPipelineShaderStageCreateInfo> shaderStages(2);
VkViewport viewport;
VkRect2D scissor;
VkPipelineColorBlendAttachmentState colorBlendAttachment;
VkPipelineLayout pipelineLayout;
VkPipeline graphicsPipeline;
VkShaderModule vertexShaderModule;
VkShaderModule fragmentShaderModule;
VmaAllocator allocator;
Mesh triangleMesh;
Mesh monkeyMesh;
VkImageView depthImageView;
AllocatedImage depthImage;
VkFormat depthFormat;
FrameData frames[frame_overlap];
VkDescriptorSetLayout globalSetLayout;
VkDescriptorSetLayout objectSetLayout;
VkDescriptorPool descriptorPool;
GPUSceneData sceneParameters;
AllocatedBuffer sceneParameterBuffer;
UploadContext uploadContext;
//std::unordered_map<std::string, Texture> loadedTextures;
VkDescriptorSet textureSet{ VK_NULL_HANDLE };
VkDescriptorSetLayout singleTextureSetLayout;
Material material;
AllocatedBuffer materialBuffer;
VkDescriptorSet materialDescriptorSet{ VK_NULL_HANDLE };
VkDescriptorSetLayout materialSetLayout;
Texture lostEmpire;
VkSampler blockySampler;

void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VkCommandBuffer cmdBuffer;
	VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
	cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdBufferAllocInfo.commandPool = uploadContext.commandPool;
	cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdBufferAllocInfo.commandBufferCount = 1;

	vkCheck(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &cmdBuffer));

	VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
	cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkCheck(vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo));

	function(cmdBuffer);

	vkCheck(vkEndCommandBuffer(cmdBuffer));

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmdBuffer;

	vkCheck(vkQueueSubmit(graphicsQueue, 1, &submitInfo, uploadContext.uploadFence));

	vkWaitForFences(device, 1, &uploadContext.uploadFence, true, 9999999999);
	vkResetFences(device, 1, &uploadContext.uploadFence);

	vkFreeCommandBuffers(device, uploadContext.commandPool, 1, &cmdBuffer);

	//clear the command pool. This will free the command buffer too
	vkResetCommandPool(device, uploadContext.commandPool, 0);
}

constexpr FrameData& GetCurrentFrame()
{
	/*
	 * Every time we render a frame, the frameNumber gets bumped by 1.
	 * This will be very useful here. With a frame overlap of 2 (the default),
	 * it means that even frames will use frames[0], while odd frames will use frames[1].
	 * While the GPU is busy executing the rendering commands from frame 0,
	 * the CPU will be writing the buffers of frame 1, and reverse.
	*/
	return frames[frameNumber % frame_overlap];
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		std::cout << "\033[1;33mWarning: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		// NOTE: temporary fix because it shows a initial error because of a json in medal tv, idk why
		if (!strstr(pCallbackData->pMessage, "medal"))
			std::cerr << "\033[1;31mError: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}
	else
	{
		std::cerr << "\033[1;36mInfo: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}

	return VK_FALSE;
}

VkShaderModule CompileShader(const char* file, shaderc_shader_kind shaderType, const char* entryPoint, const char* shaderName)
{
	std::string shaderSource;
	std::ifstream in(file, std::ios::in | std::ios::binary);
	if (in)
	{
		in.seekg(0, std::ios::end);
		shaderSource.resize(in.tellg());
		in.seekg(0, std::ios::beg);
		in.read(&shaderSource[0], shaderSource.size());
	}
	else
	{
		__debugbreak();
	}
	in.close();

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
	options.SetWarningsAsErrors();
	options.SetGenerateDebugInfo();
	options.SetSourceLanguage(shaderc_source_language_glsl);
	shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(shaderSource,
																	 shaderType,
																	 shaderName,
																	 entryPoint,
																	 options);
	if (module.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		std::cout << module.GetErrorMessage() << std::endl;
		__debugbreak();
	}

	std::vector<uint32_t> compiledShader = std::vector<uint32_t>(module.cbegin(), module.cend());
	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = compiledShader.size() * sizeof(uint32_t);
	createInfo.pCode = compiledShader.data();

	VkShaderModule shaderModule = 0;
	vkCheck(vkCreateShaderModule(device, &createInfo, 0, &shaderModule));

	return shaderModule;
}

VertexInputDescription Vertex::GetVertexDescription()
{
	VertexInputDescription description;

	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	description.bindings.push_back(mainBinding);

	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(Vertex, position);

	VkVertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	normalAttribute.offset = offsetof(Vertex, normal);

	VkVertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	colorAttribute.offset = offsetof(Vertex, color);

	VkVertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
	uvAttribute.offset = offsetof(Vertex, uv);

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);

	return description;
}

bool Mesh::loadFromGLTF(const char* file)
{
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err;
	std::string warn;

	bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, file);
	//bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, argv[1]); // for binary glTF(.glb)

	if (!warn.empty()) {
		printf("Warn: %s\n", warn.c_str());
	}

	if (!err.empty()) {
		printf("Err: %s\n", err.c_str());
	}

	if (!ret) {
		printf("Failed to parse glTF\n");
		return -1;
	}

	auto& gltf_mesh = model.meshes.at(0);
	auto& gltf_primitive = gltf_mesh.primitives.at(0);
}

bool Mesh::loadFromObj(const char* file, const char* material_path = "")
{
	//attrib will contain the vertex arrays of the file
	tinyobj::attrib_t attrib;
	//shapes contains the info for each separate object in the file
	std::vector<tinyobj::shape_t> shapes;
	//materials contains the information about the material of each shape, but we won't use it.
	std::vector<tinyobj::material_t> materials;

	//error and warning output from the load function
	std::string warn;
	std::string err;

	tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, file, material_path);
	if (!warn.empty())
	{
		std::cout << "WARN: " << warn << std::endl;
	}

	if (!err.empty())
	{
		std::cerr << err << std::endl;
		return false;
	}

	// Loop over shapes
	for (size_t s = 0; s < shapes.size(); s++)
	{
		// Loop over faces(polygon)
		size_t indexOffset = 0;
		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
		{
			//hardcode loading to triangles
			int fv = 3;

			// Loop over vertices in the face
			for (size_t v = 0; v < fv; v++)
			{
				// access to vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[indexOffset + v];

				//vertex position
				tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
				tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
				tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
				//vertex normal
				tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
				tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
				tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];

				if (idx.texcoord_index < 0) idx.texcoord_index = 0;
				tinyobj::real_t ux = attrib.texcoords[2 * idx.texcoord_index + 0];
				tinyobj::real_t uy = attrib.texcoords[2 * idx.texcoord_index + 1];

				Vertex new_vert;
				//copy it into our vertex
				new_vert.position.x = vx;
				new_vert.position.y = vy;
				new_vert.position.z = vz;

				new_vert.normal.x = nx;
				new_vert.normal.y = ny;
				new_vert.normal.z = nz;

				new_vert.uv.x = ux;
				new_vert.uv.y = 1 - uy; //do the 1-y on the uv.y because Vulkan UV coordinates work like that.

				//we are setting the vertex color as the vertex normal. This is just for display purposes
				auto id = shapes[s].mesh.material_ids[f];
				if(id > -1)
					new_vert.color = glm::vec3(materials[id].diffuse[0], materials[id].diffuse[1], materials[id].diffuse[2]);
				else
					new_vert.color = glm::vec3(1, 1, 1);

				vertices.push_back(new_vert);

			}
			indexOffset += fv;
		}

	}
}

VkImageCreateInfo ImageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent)
{
	VkImageCreateInfo info = { };
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent = extent;
	info.mipLevels = 1;
	info.arrayLayers = 1; // used for cubemaps for example, where you have 6 layers of images
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usageFlags;

	return info;
}

VkImageViewCreateInfo ImageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags)
{
	// build a image - view for the depth image to use for rendering
	VkImageViewCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D; // could use VK_IMAGE_VIEW_TYPE_CUBE for cubemaps
	info.image = image;
	info.format = format;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = 1;
	info.subresourceRange.aspectMask = aspectFlags;

	return info;
}

bool LoadFromImage(const char* file, AllocatedImage& outImage)
{
	int texWidth, texHeight, texChannels;

	stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
	{
		std::cout << "Failed to load texture file " << file << std::endl;
		return false;
	}

	void* pixelPtr = pixels;
	// We calculate image sizes by doing 4 bytes per pixel, and texWidth * texHeight number of pixels.
	VkDeviceSize imageSize = texWidth * texHeight * 4;

	VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = imageSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;


	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	vkCheck(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo,
							&stagingBuffer.buffer,
							&stagingBuffer.allocation,
							nullptr));

	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation, &data);

	memcpy(data, pixelPtr, static_cast<size_t>(imageSize));

	vmaUnmapMemory(allocator, stagingBuffer.allocation);
	//we no longer need the loaded data, so we can free the pixels as they are now in the staging buffer
	stbi_image_free(pixels);

	VkExtent3D imageExtent;
	imageExtent.width = texWidth;
	imageExtent.height = texHeight;
	imageExtent.depth = 1;

	VkImageCreateInfo imgInfo = ImageCreateInfo(imageFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

	AllocatedImage image;

	VmaAllocationCreateInfo imgAllocInfo = {};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	vmaCreateImage(allocator, &imgInfo, &imgAllocInfo, &image.image, &image.allocation, nullptr);

	immediate_submit([&](VkCommandBuffer cmd) {
		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.levelCount = 1;
		range.baseMipLevel = 0;
		range.layerCount = 1;
		range.baseArrayLayer = 0;

		VkImageMemoryBarrier imageBarrierToTransfer = {};
		imageBarrierToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageBarrierToTransfer.srcAccessMask = 0;
		imageBarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrierToTransfer.image = image.image;
		imageBarrierToTransfer.subresourceRange = range;

		vkCmdPipelineBarrier(cmd,
							 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT,
							 0, 0, nullptr, 0, nullptr, 1,
							 &imageBarrierToTransfer);

		VkBufferImageCopy copyRegion = {};
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = imageExtent;

		//copy the buffer into the image
		vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		VkImageMemoryBarrier imageBarrierToRead = {};
		imageBarrierToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageBarrierToRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrierToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageBarrierToRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrierToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageBarrierToRead.image = image.image;
		imageBarrierToRead.subresourceRange = range;

		vkCmdPipelineBarrier(cmd,
							 VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
							 0, 0, nullptr, 0, nullptr, 1,
							 &imageBarrierToRead);
	});

	vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);

	outImage = image;

	return true;
}

void LoadImages()
{
	/*Texture lostEmpire;
	LoadFromImage("assets/lost-empire-rgba", lostEmpire.image);

	VkImageViewCreateInfo lostEmpireImageInfo = ImageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, lostEmpire.image.image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(device, &lostEmpireImageInfo, nullptr, &lostEmpire.imageView);

	loadedTextures["empire_diffuse"] = lostEmpire;*/
}

VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t binding)
{
	VkDescriptorSetLayoutBinding setbind = {};
	setbind.binding = binding;
	setbind.descriptorCount = 1;
	setbind.descriptorType = type;
	setbind.pImmutableSamplers = nullptr;
	setbind.stageFlags = stageFlags;

	return setbind;
}

VkWriteDescriptorSet WriteDescriptorBuffer(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorBufferInfo* bufferInfo, uint32_t binding)
{
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.pNext = nullptr;

	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pBufferInfo = bufferInfo;

	return write;
}

VkSamplerCreateInfo SamplerCreateInfo(VkFilter filters, VkSamplerAddressMode samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT)
{
	VkSamplerCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter = filters;
	info.minFilter = filters;
	info.addressModeU = samplerAddressMode;
	info.addressModeV = samplerAddressMode;
	info.addressModeW = samplerAddressMode;

	return info;
}
VkWriteDescriptorSet WriteDescriptorImage(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorImageInfo* imageInfo, uint32_t binding)
{
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pImageInfo = imageInfo;

	return write;
}

void CreatePipeline()
{
	// Init pipeline
	vertexShaderModule = CompileShader("src/shaders/triangle.vert.glsl", shaderc_vertex_shader, "main", "vertex shader");
	fragmentShaderModule = CompileShader("src/shaders/triangle.frag.glsl", shaderc_fragment_shader, "main", "fragment shader");

	VkPipelineShaderStageCreateInfo vertexShaderStageInfo = {};
	vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertexShaderStageInfo.module = vertexShaderModule;
	vertexShaderStageInfo.pName = "main";
	shaderStages[0] = vertexShaderStageInfo;

	VkPipelineShaderStageCreateInfo fragmentShaderStageInfo = {};
	fragmentShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragmentShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragmentShaderStageInfo.module = fragmentShaderModule;
	fragmentShaderStageInfo.pName = "main";
	shaderStages[1] = fragmentShaderStageInfo;

	VertexInputDescription vertexDescription = Vertex::GetVertexDescription();
	VkPipelineVertexInputStateCreateInfo vertexInputStateInfo = {};
	vertexInputStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputStateInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();
	vertexInputStateInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	vertexInputStateInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();
	vertexInputStateInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.primitiveRestartEnable = false;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRasterizationStateCreateInfo rasterizationStateInfo = {};
	rasterizationStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisamplingStateInfo = {};
	multisamplingStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisamplingStateInfo.minSampleShading = 1.0f;
	multisamplingStateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
	colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachmentState.blendEnable = false;

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {};
	colorBlendStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateInfo.logicOpEnable = false;
	colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateInfo.attachmentCount = 1;
	colorBlendStateInfo.pAttachments = &colorBlendAttachmentState;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo = {};
	depthStencilStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateInfo.depthTestEnable = true;
	depthStencilStateInfo.depthWriteEnable = true;
	depthStencilStateInfo.depthBoundsTestEnable = false;
	depthStencilStateInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilStateInfo.minDepthBounds = 0.0f;
	depthStencilStateInfo.maxDepthBounds = 1.0f;
	depthStencilStateInfo.stencilTestEnable = false;

	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	viewport.x = 0.0f;
	viewport.y = 0.0f;

	scissor.extent = { width, height };
	scissor.offset = { 0,0 };

	VkPipelineViewportStateCreateInfo viewportStateInfo = {};
	viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateInfo.scissorCount = 1;
	viewportStateInfo.pScissors = &scissor;
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.pViewports = &viewport;

	VkPushConstantRange meshConstantRange;
	meshConstantRange.size = sizeof(MeshPushConstants);
	meshConstantRange.offset = 0;
	meshConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayout layouts[] = { globalSetLayout, objectSetLayout, singleTextureSetLayout, materialSetLayout };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;
	pipelineLayoutInfo.setLayoutCount = ARRAYSIZE(layouts);
	pipelineLayoutInfo.pSetLayouts = layouts;

	vkCheck(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;
	pipelineInfo.stageCount = shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputStateInfo;
	pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	pipelineInfo.pViewportState = &viewportStateInfo;
	pipelineInfo.pRasterizationState = &rasterizationStateInfo;
	pipelineInfo.pMultisampleState = &multisamplingStateInfo;
	pipelineInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineInfo.pDepthStencilState = &depthStencilStateInfo;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;

	vkCheck(vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &graphicsPipeline));
}

size_t pad_uniform_buffer_size(size_t originalSize)
{
	// Calculate required alignment based on minimum device offset alignment
	size_t minUboAlignment = gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if (minUboAlignment > 0)
	{
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}
	return alignedSize;
}

void Init(GLFWwindow* window)
{
	// Init vulkan core
	vkb::InstanceBuilder instanceBuilder;

	auto instanceResult = instanceBuilder.set_app_name("Vulkan")
		.request_validation_layers(true)
		.require_api_version(1, 2, 0)
		.desire_api_version(1, 2, 0)
		.set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		.set_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
		.set_debug_callback(debugCallback)
		.build();

	vkb::Instance vkbInstance = instanceResult.value();

	instance = vkbInstance.instance;
	debugMessenger = vkbInstance.debug_messenger;

	glfwCreateWindowSurface(instance, window, nullptr, &surface);

	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 2).set_surface(surface).select().value();

	VkPhysicalDeviceVulkan11Features physicalDeviceVulkan11Features = {};
	physicalDeviceVulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	physicalDeviceVulkan11Features.shaderDrawParameters = true;

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device vkbDevice = deviceBuilder.add_pNext(&physicalDeviceVulkan11Features).build().value();

	device = vkbDevice.device;
	chosenGPU = physicalDevice.physical_device;

	graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo vmaAllocatorInfo = {};
	vmaAllocatorInfo.physicalDevice = chosenGPU;
	vmaAllocatorInfo.device = device;
	vmaAllocatorInfo.instance = instance;

	vkCheck(vmaCreateAllocator(&vmaAllocatorInfo, &allocator));

	vkGetPhysicalDeviceProperties(chosenGPU, &gpuProperties);
	std::cout << "The GPU has a minimum buffer alignment of " << gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;

	// Init swapchain
	vkb::SwapchainBuilder swapchainBuilder{ physicalDevice, device, surface };
	vkb::Swapchain vkbSwapchain = swapchainBuilder.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.build()
		.value();

	swapchain = vkbSwapchain.swapchain;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();
	swapchainImageFormat = vkbSwapchain.image_format;

	VkExtent3D depthImageExtent = {
		width,
		height,
		1
	};

	depthFormat = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo depthImageInfo = ImageCreateInfo(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

	VmaAllocationCreateInfo depthImageAllocInfo = {};
	depthImageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	depthImageAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	vmaCreateImage(allocator, &depthImageInfo, &depthImageAllocInfo, &depthImage.image, &depthImage.allocation, nullptr);

	VkImageViewCreateInfo depthImageViewInfo = ImageViewCreateInfo(depthFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	vkCheck(vkCreateImageView(device, &depthImageViewInfo, nullptr, &depthImageView));

	// Init commands
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.queueFamilyIndex = graphicsQueueFamily;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	for (int i = 0; i < frame_overlap; i++)
	{
		vkCheck(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frames[i].commandPool));
		VkCommandBufferAllocateInfo commandBufferInfo = {};
		commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandBufferInfo.commandBufferCount = 1;
		commandBufferInfo.commandPool = frames[i].commandPool;
		commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		vkCheck(vkAllocateCommandBuffers(device, &commandBufferInfo, &frames[i].mainCommandBuffer));
	}

	VkCommandPoolCreateInfo uploadCommandPoolInfo = {};
	uploadCommandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	uploadCommandPoolInfo.queueFamilyIndex = graphicsQueueFamily;
	uploadCommandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	vkCheck(vkCreateCommandPool(device, &uploadCommandPoolInfo, nullptr, &uploadContext.commandPool));

	// Init framebuffer
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = swapchainImageFormat;
	//1 sample, we won't be doing MSAA
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	// we Clear when this attachment is loaded
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	// we keep the attachment stored when the renderpass ends
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	//we don't care about stencil
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	//we don't know or care about the starting layout of the attachment
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	//after the renderpass ends, the image has to be on a layout ready for display
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	//attachment number will index into the pAttachments array in the parent renderpass itself
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


	VkSubpassDescription subpass = {};
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;


	VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	vkCheck(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));

	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.width = width;
	framebufferInfo.height = height;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.layers = 1;

	const uint32_t swapchainImageCount = swapchainImages.size();
	framebuffers.resize(swapchainImageCount);

	for (int i = 0; i < swapchainImageCount; i++)
	{
		VkImageView attachments[2] = { swapchainImageViews[i], depthImageView };

		framebufferInfo.attachmentCount = 2;
		framebufferInfo.pAttachments = attachments;
		vkCheck(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]));
	}

	// Init sync structures
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	//we want to create the fence with the Create Signaled flag, 
	// so we can wait on it before using it on a GPU command (for the first frame)
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (int i = 0; i < frame_overlap; i++)
	{
		vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &frames[i].renderFence));

		vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].renderSemaphore));
		vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].presentSemaphore));
	}

	VkFenceCreateInfo uploadFenceCreateInfo = {};
	uploadFenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	vkCheck(vkCreateFence(device, &uploadFenceCreateInfo, nullptr, &uploadContext.uploadFence));

	// Init descriptors
	// create a descriptor pool that will hold 10 uniform buffers
	std::vector<VkDescriptorPoolSize> sizes = {
												{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
												{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
												{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
												//add combined-image-sampler descriptor types to the pool
												{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 }
	};
	/*
	 * When creating a descriptor pool, you need to specify how many descriptors of each type you will need,
	 * and what�s the maximum number of sets to allocate from it.
	 */
	 // Descriptor Pool
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.maxSets = 10;
	descriptorPoolInfo.poolSizeCount = sizes.size();
	descriptorPoolInfo.pPoolSizes = sizes.data();

	vkCheck(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

	// Camera set layout binding (uniform buffer)
	VkDescriptorSetLayoutBinding cameraBufferBinding = DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
																				  VK_SHADER_STAGE_VERTEX_BIT,
																				  0);

	// scene buffer
	const size_t sceneParamBufferSize = frame_overlap * pad_uniform_buffer_size(sizeof(GPUSceneData));
	VkBufferCreateInfo sceneBufferInfo = {};
	sceneBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	sceneBufferInfo.size = sceneParamBufferSize;
	sceneBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;


	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	vkCheck(vmaCreateBuffer(allocator, &sceneBufferInfo, &vmaallocInfo,
							&sceneParameterBuffer.buffer,
							&sceneParameterBuffer.allocation,
							nullptr));

	// scene set layout binding (dynamic uniform buffer)
	VkDescriptorSetLayoutBinding sceneBufferBinding = DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
																				 VK_SHADER_STAGE_VERTEX_BIT |
																				 VK_SHADER_STAGE_FRAGMENT_BIT,
																				 1);

	// Create set layout #0
	VkDescriptorSetLayoutBinding bindings[] = { cameraBufferBinding, sceneBufferBinding };
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
	descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutInfo.bindingCount = ARRAYSIZE(bindings);
	descriptorSetLayoutInfo.pBindings = bindings;
	vkCheck(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &globalSetLayout));

	// object set layout binding (storage buffer)
	VkDescriptorSetLayoutBinding objectBinding = DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
																			VK_SHADER_STAGE_VERTEX_BIT,
																			0);

	// Create set layout #1
	VkDescriptorSetLayoutCreateInfo objectSetLayoutInfo = {};
	objectSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	objectSetLayoutInfo.bindingCount = 1;
	objectSetLayoutInfo.pBindings = &objectBinding;
	vkCheck(vkCreateDescriptorSetLayout(device, &objectSetLayoutInfo, nullptr, &objectSetLayout));

	// Create texture set layout #2
	VkDescriptorSetLayoutBinding textureBind = DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
																		  VK_SHADER_STAGE_FRAGMENT_BIT, 
																		  0);

	VkDescriptorSetLayoutCreateInfo textureSetInfo = {};
	textureSetInfo.bindingCount = 1;
	textureSetInfo.flags = 0;
	textureSetInfo.pNext = nullptr;
	textureSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	textureSetInfo.pBindings = &textureBind;

	vkCheck(vkCreateDescriptorSetLayout(device, &textureSetInfo, nullptr, &singleTextureSetLayout));

	// Create Material buffer
	VkBufferCreateInfo materialBufferInfo = {};
	materialBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	materialBufferInfo.size = sizeof(Material);
	materialBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	// Not using this because its already declared
	//VmaAllocationCreateInfo vmaallocInfo = {};
	//vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	vkCheck(vmaCreateBuffer(allocator, &materialBufferInfo, &vmaallocInfo,
							&materialBuffer.buffer,
							&materialBuffer.allocation,
							nullptr));

	// Material set layout binding (dynamic uniform buffer)
	VkDescriptorSetLayoutBinding materialBufferBinding = DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
																					VK_SHADER_STAGE_VERTEX_BIT |
																					VK_SHADER_STAGE_FRAGMENT_BIT,
																					0);
	VkDescriptorSetLayoutCreateInfo materialSetLayoutInfo = {};
	materialSetLayoutInfo.bindingCount = 1;
	materialSetLayoutInfo.flags = 0;
	materialSetLayoutInfo.pNext = nullptr;
	materialSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	materialSetLayoutInfo.pBindings = &materialBufferBinding;

	vkCreateDescriptorSetLayout(device, &materialSetLayoutInfo, nullptr, &materialSetLayout);

	VkDescriptorSetAllocateInfo materialSetAllocInfo = {};
	materialSetAllocInfo.pNext = nullptr;
	materialSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	materialSetAllocInfo.descriptorPool = descriptorPool;
	materialSetAllocInfo.descriptorSetCount = 1;
	materialSetAllocInfo.pSetLayouts = &materialSetLayout;

	vkAllocateDescriptorSets(device, &materialSetAllocInfo, &materialDescriptorSet);

	// Write into the material descriptor buffer
	VkDescriptorBufferInfo materialDescriptorBufferInfo = {};
	materialDescriptorBufferInfo.buffer = materialBuffer.buffer;
	materialDescriptorBufferInfo.offset = 0;
	materialDescriptorBufferInfo.range = sizeof(Material);

	VkWriteDescriptorSet materialWrite = WriteDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
															materialDescriptorSet,
															&materialDescriptorBufferInfo,
															0);

	vkUpdateDescriptorSets(device, 1, &materialWrite, 0, nullptr);

	for (int i = 0; i < frame_overlap; i++)
	{
		// Uniform Buffer
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = sizeof(GPUCameraData);
		bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;


		VmaAllocationCreateInfo vmaallocInfo = {};
		vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		AllocatedBuffer newBuffer;

		//allocate the buffer
		vkCheck(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo,
								&newBuffer.buffer,
								&newBuffer.allocation,
								nullptr));

		frames[i].cameraBuffer = newBuffer;

		// Storage buffer
		const int MAX_OBJECTS = 10000;
		bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = sizeof(GPUObjectData) * MAX_OBJECTS;
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;


		vmaallocInfo = {};
		vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		vkCheck(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo,
								&newBuffer.buffer,
								&newBuffer.allocation,
								nullptr));

		frames[i].objectBuffer = newBuffer;

		// Allocate Descriptor sets
		VkDescriptorSetAllocateInfo cameraDescriptorSetAllocInfo = {};
		cameraDescriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		cameraDescriptorSetAllocInfo.descriptorPool = descriptorPool;
		cameraDescriptorSetAllocInfo.descriptorSetCount = 1;
		cameraDescriptorSetAllocInfo.pSetLayouts = &globalSetLayout;
		vkCheck(vkAllocateDescriptorSets(device, &cameraDescriptorSetAllocInfo, &frames[i].globalDescriptorSet));

		VkDescriptorSetAllocateInfo objectDescriptorSetAllocInfo = {};
		objectDescriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		objectDescriptorSetAllocInfo.descriptorPool = descriptorPool;
		objectDescriptorSetAllocInfo.descriptorSetCount = 1;
		objectDescriptorSetAllocInfo.pSetLayouts = &objectSetLayout;
		vkCheck(vkAllocateDescriptorSets(device, &objectDescriptorSetAllocInfo, &frames[i].objectDescriptorSet));

		// Write into the camera descriptor buffer
		VkDescriptorBufferInfo cameraBufferInfo = {};
		cameraBufferInfo.buffer = frames[i].cameraBuffer.buffer;
		cameraBufferInfo.offset = 0;
		cameraBufferInfo.range = sizeof(GPUCameraData);

		VkWriteDescriptorSet cameraWrite = WriteDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
																 frames[i].globalDescriptorSet,
																 &cameraBufferInfo,
																 0);

		// Write into the scene descriptor buffer
		VkDescriptorBufferInfo sceneBufferInfo = {};
		sceneBufferInfo.buffer = sceneParameterBuffer.buffer;
		sceneBufferInfo.offset = 0;
		sceneBufferInfo.range = sizeof(GPUSceneData);

		VkWriteDescriptorSet sceneWrite = WriteDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
																frames[i].globalDescriptorSet,
																&sceneBufferInfo,
																1);

		// Write into the object descriptor buffer
		VkDescriptorBufferInfo objectBufferInfo = {};
		objectBufferInfo.buffer = frames[i].objectBuffer.buffer;
		objectBufferInfo.offset = 0;
		objectBufferInfo.range = sizeof(GPUObjectData);

		VkWriteDescriptorSet objectWrite = WriteDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
																 frames[i].objectDescriptorSet,
																 &objectBufferInfo,
																 0);


		VkWriteDescriptorSet setWrites[] = { cameraWrite, sceneWrite, objectWrite };
		vkUpdateDescriptorSets(device, ARRAYSIZE(setWrites), setWrites, 0, nullptr);
	}

	// Init textures
	LoadFromImage("assets/lost_empire-RGBA.png", lostEmpire.image);

	VkImageViewCreateInfo lostEmpireImageInfo = ImageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, lostEmpire.image.image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(device, &lostEmpireImageInfo, nullptr, &lostEmpire.imageView);

	VkSamplerCreateInfo samplerInfo = SamplerCreateInfo(VK_FILTER_NEAREST);

	vkCreateSampler(device, &samplerInfo, nullptr, &blockySampler);

	VkDescriptorSetAllocateInfo textureSetAllocInfo = {};
	textureSetAllocInfo.pNext = nullptr;
	textureSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	textureSetAllocInfo.descriptorPool = descriptorPool;
	textureSetAllocInfo.descriptorSetCount = 1;
	textureSetAllocInfo.pSetLayouts = &singleTextureSetLayout;

	vkAllocateDescriptorSets(device, &textureSetAllocInfo, &textureSet);

	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = blockySampler;
	imageBufferInfo.imageView = lostEmpire.imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = WriteDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(device, 1, &texture1, 0, nullptr);


	// Init mesh
	triangleMesh.vertices.resize(3);
	triangleMesh.vertices[0].position = { 0.5f,  0.5f, 0.0f };
	triangleMesh.vertices[1].position = { -0.5f,  0.5f, 0.0f };
	triangleMesh.vertices[2].position = { 0.0f, -0.5f, 0.0f };
	triangleMesh.vertices[0].color = { 1.0f, 0.0f, 0.0f };
	triangleMesh.vertices[1].color = { 0.0f, 1.0f, 0.0f };
	triangleMesh.vertices[2].color = { 0.0f, 0.0f, 1.0f };

	monkeyMesh.loadFromObj("assets/knot.obj", "assets/");
	//monkeyMesh.loadFromGLTF("assets/gas_stations_fixed/scene.gltf");

	// Upload mesh
	VkBufferCreateInfo stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.size = monkeyMesh.vertices.size() * sizeof(Vertex);
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingVMAAllocInfo = {};
	stagingVMAAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	vkCheck(vmaCreateBuffer(allocator, &stagingBufferInfo, &stagingVMAAllocInfo,
							&stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation, &data);
	memcpy(data, monkeyMesh.vertices.data(), monkeyMesh.vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(allocator, stagingBuffer.allocation);

	VkBufferCreateInfo meshBufferInfo = {};
	meshBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	meshBufferInfo.size = monkeyMesh.vertices.size() * sizeof(Vertex);
	meshBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo meshVMAAllocInfo = {};
	meshVMAAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	vkCheck(vmaCreateBuffer(allocator, &meshBufferInfo, &meshVMAAllocInfo,
							&monkeyMesh.vertexBuffer.buffer, &monkeyMesh.vertexBuffer.allocation, nullptr));

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = monkeyMesh.vertices.size() * sizeof(Vertex);
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, monkeyMesh.vertexBuffer.buffer, 1, &copy);
	});

	vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);

	CreatePipeline();
}

// Camera stuff
glm::vec3 cameraPos = { 0, -10, 0 };
glm::vec3 cameraFront = { 0, 0, 1 };
glm::vec3 cameraTarget = { 0, 0, 0 };
glm::vec3 cameraUp = { 0, 1, 0 };
float deltaTime = 1.0f;
float lastFrame = 0.0f;
float lastX = width / 2;
float lastY = height / 2;
float yaw = 0.0f;
float pitch = 0.0f;
float sensitivity = 0.2f;
float timer = 0;
double x = 0, y = 0;
bool firstTimeMouse = true;

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_2))
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

		if (firstTimeMouse)
		{
			lastX = xpos;
			lastY = ypos;
			firstTimeMouse = false;
		}

		glfwGetCursorPos(window, &x, &y);
		float xoffset = static_cast<float>(x) - lastX;
		float yoffset = lastY - static_cast<float>(y);
		lastX = static_cast<float>(x);
		lastY = static_cast<float>(y);
		xoffset *= sensitivity;
		yoffset *= sensitivity;

		yaw += xoffset;
		pitch += yoffset;

		if (pitch > 89.f)
			pitch = 89.f;
		if (pitch < -89.f)
			pitch = -89.f;

		glm::vec3 front;
		front.x = -(sin(glm::radians(yaw)) * cos(glm::radians(pitch)));
		front.y = -(sin(glm::radians(pitch)));
		front.z = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
		cameraFront = glm::normalize(front);
	}
	else
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		firstTimeMouse = true;
	}
}

void Render(GLFWwindow* window)
{
	vkCheck(vkWaitForFences(device, 1, &GetCurrentFrame().renderFence, true, 1000000000));
	vkCheck(vkResetFences(device, 1, &GetCurrentFrame().renderFence));

	vkCheck(vkResetCommandBuffer(GetCurrentFrame().mainCommandBuffer, NULL));

	uint32_t frameIndex;
	vkCheck(vkAcquireNextImageKHR(device, swapchain, 1000000000, GetCurrentFrame().presentSemaphore, nullptr, &frameIndex));

	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkCheck(vkBeginCommandBuffer(GetCurrentFrame().mainCommandBuffer, &cmdBeginInfo));

	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.2f, 1.0f, 1.0f } };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkClearValue clearValues[2] = { clearValue, depthClear };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.framebuffer = framebuffers[frameIndex];
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.renderPass = renderPass;
	renderPassBeginInfo.renderArea.extent = { width, height };
	renderPassBeginInfo.renderArea.offset = { 0, 0 };

	// Camera
	float cameraSpeed = 10.0f * deltaTime;
	float currentFrame = static_cast<float>(glfwGetTime());
	deltaTime = currentFrame - lastFrame;
	lastFrame = currentFrame;

	if (glfwGetKey(window, GLFW_KEY_W))
		cameraPos -= cameraSpeed * cameraFront;
	if (glfwGetKey(window, GLFW_KEY_S))
		cameraPos += cameraSpeed * cameraFront;
	if (glfwGetKey(window, GLFW_KEY_D))
		cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_A))
		cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_SPACE))
		cameraPos.y += cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT))
		cameraPos.y -= cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_ESCAPE))
		glfwSetWindowShouldClose(window, true);

	glfwSetCursorPosCallback(window, mouse_callback);

	//make a model view matrix for rendering the object
	//camera position
	glm::vec3 camPos = { 0.f,0.f,-2.f };

	//glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
	glm::mat4 view = glm::lookAtLH(cameraPos, cameraPos + cameraFront, cameraUp);
	//camera projection
	glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)width / (float)height, 0.1f, 1000.0f);
	projection[1][1] *= -1;
	glm::mat4 model = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 5, -12, -5 }) * glm::scale(glm::mat4(1.0f), glm::vec3(0.08f, 0.08f, 0.08f));

	//calculate final mesh matrix
	glm::mat4 meshMatrix = projection * view * model;

	// Uniform buffers
	GPUCameraData cameraData;
	cameraData.projection = projection;
	cameraData.view = view;
	cameraData.viewproj = projection * view;
	cameraData.position = glm::vec4(cameraPos, 1.0f);

	void* camData;
	vmaMapMemory(allocator, GetCurrentFrame().cameraBuffer.allocation, &camData);
	memcpy(camData, &cameraData, sizeof(GPUCameraData));
	vmaUnmapMemory(allocator, GetCurrentFrame().cameraBuffer.allocation);

	float framed = (frameNumber / 5500.f);
	sceneParameters.ambientColor = { sin(framed),0,cos(framed),1 };
	char* sceneData;
	vmaMapMemory(allocator, sceneParameterBuffer.allocation, (void**)&sceneData);
	int frameI = frameNumber % frame_overlap;
	sceneData += pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameI;
	memcpy(sceneData, &sceneParameters, sizeof(GPUSceneData));
	vmaUnmapMemory(allocator, sceneParameterBuffer.allocation);

	// Begin Render pass
	vkCmdBeginRenderPass(GetCurrentFrame().mainCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(GetCurrentFrame().mainCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

	VkDeviceSize offset = { 0 };
	vkCmdBindVertexBuffers(GetCurrentFrame().mainCommandBuffer, 0, 1, &monkeyMesh.vertexBuffer.buffer, &offset);

	//offset for our scene buffer
	uint32_t uniform_offset = pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameI;
	// Bind global descriptor set (descriptor set #0)
	vkCmdBindDescriptorSets(GetCurrentFrame().mainCommandBuffer,
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							pipelineLayout,
							0, 1,
							&GetCurrentFrame().globalDescriptorSet,
							1, &uniform_offset);

	// Storage buffer
	void* objectData;
	vmaMapMemory(allocator, GetCurrentFrame().objectBuffer.allocation, &objectData);
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;
	objectSSBO->modelMatrix = model;
	vmaUnmapMemory(allocator, GetCurrentFrame().objectBuffer.allocation);

	// Bind object descriptor set (descriptor set #1)
	vkCmdBindDescriptorSets(GetCurrentFrame().mainCommandBuffer,
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							pipelineLayout,
							1, 1,
							&GetCurrentFrame().objectDescriptorSet,
							0, nullptr);

	// Bind texture descriptor set (descriptor set #2)
	vkCmdBindDescriptorSets(GetCurrentFrame().mainCommandBuffer,
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							pipelineLayout,
							2, 1,
							&textureSet,
							0, nullptr);

	// Material Descriptor Set
	Material materialConstants;
	materialConstants.ambient = glm::vec4(1.0f, 0.5f, 0.31f, 1.0f);
	materialConstants.diffuse = glm::vec4(1.0f, 0.5f, 0.31f, 1.0f);
	materialConstants.specular = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	materialConstants.shininess = glm::vec4(32.0f, 0.0f, 0.0f, 1.0f);
	void* materialData;
	vmaMapMemory(allocator, materialBuffer.allocation, &materialData);
	memcpy(materialData, &materialConstants, sizeof(Material));
	vmaUnmapMemory(allocator, materialBuffer.allocation);
	uint32_t materialOffset = 0;
	vkCmdBindDescriptorSets(GetCurrentFrame().mainCommandBuffer,
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							pipelineLayout,
							3, 1,
							&materialDescriptorSet,
							1, &materialOffset);



	// Push Constant
	MeshPushConstants constants;
	constants.renderMatrix = model;
	//vkCmdPushConstants(GetCurrentFrame().mainCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);


	vkCmdDraw(GetCurrentFrame().mainCommandBuffer, monkeyMesh.vertices.size(), 1, 0, 0);

	// Record dear imgui primitives into command buffer
	ImGui_ImplVulkan_RenderDrawData(draw_data, GetCurrentFrame().mainCommandBuffer);

	vkCmdEndRenderPass(GetCurrentFrame().mainCommandBuffer);
	vkCheck(vkEndCommandBuffer(GetCurrentFrame().mainCommandBuffer));

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &GetCurrentFrame().mainCommandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &GetCurrentFrame().renderSemaphore;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &GetCurrentFrame().presentSemaphore;
	submitInfo.pWaitDstStageMask = &waitStage;

	vkCheck(vkQueueSubmit(graphicsQueue, 1, &submitInfo, GetCurrentFrame().renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &GetCurrentFrame().renderSemaphore;
	presentInfo.pImageIndices = &frameIndex;

	vkCheck(vkQueuePresentKHR(graphicsQueue, &presentInfo));

	frameNumber++;
}

int main()
{
	int rc = glfwInit();
	assert(rc);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	GLFWwindow* window = glfwCreateWindow(width, height, "Vulkan", 0, 0);
	assert(window);

	Init(window);

	int frameCount = 0;
	double previousTime = glfwGetTime();


	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForVulkan(window, true);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = instance;
	init_info.PhysicalDevice = chosenGPU;
	init_info.Device = device;
	init_info.QueueFamily = graphicsQueueFamily;
	init_info.Queue = graphicsQueue;
	init_info.PipelineCache = nullptr;
	init_info.DescriptorPool = descriptorPool;
	init_info.Allocator = nullptr;
	init_info.MinImageCount = 2;
	init_info.ImageCount = 3;
	init_info.CheckVkResultFn = nullptr;
	ImGui_ImplVulkan_Init(&init_info, renderPass);

	// Upload Fonts
	{
		// Use any command queue
		VkCommandPool command_pool = GetCurrentFrame().commandPool;
		VkCommandBuffer command_buffer = GetCurrentFrame().mainCommandBuffer;

		vkCheck(vkResetCommandPool(device, command_pool, 0));
		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkCheck(vkBeginCommandBuffer(command_buffer, &begin_info));

		ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

		VkSubmitInfo end_info = {};
		end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		end_info.commandBufferCount = 1;
		end_info.pCommandBuffers = &command_buffer;
		vkCheck(vkEndCommandBuffer(command_buffer));
		vkCheck(vkQueueSubmit(graphicsQueue, 1, &end_info, VK_NULL_HANDLE));

		vkCheck(vkDeviceWaitIdle(device));
		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}

	bool open = true;
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

	float deltaTime = 0;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		
		// Measure speed
		deltaTime = float(std::max(0.0, Timer::elapsed() / 1000.0));
		Timer::record();

		// Imgui stuff
		// Start the Dear ImGui frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		std::stringstream ss;
		ss << "Vulkan -> " << std::setprecision(2) << deltaTime * 1000.0f << "ms";
		glfwSetWindowTitle(window, ss.str().c_str());

		if (ImGui::Begin("Example: Simple overlay", &open, window_flags))
		{
			ImGui::Text("Render Time: %.1f ms", deltaTime * 1000.0f);
			ImGui::Separator();
			if (ImGui::Button("Reload Shaders"))
			{
				CreatePipeline();
			}
		}
		ImGui::End();

		if (glfwGetKey(window, GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window, true);

		ImGui::Render();
		draw_data = ImGui::GetDrawData();
		Render(window);
	}

	vkDeviceWaitIdle(device);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	vkDestroyDescriptorSetLayout(device, singleTextureSetLayout, nullptr);
	vkDestroySampler(device, blockySampler, nullptr);
	vkDestroyImageView(device, lostEmpire.imageView, nullptr);
	vkDestroyFence(device, uploadContext.uploadFence, nullptr);
	vkDestroyCommandPool(device, uploadContext.commandPool, nullptr);
	vmaDestroyBuffer(allocator, triangleMesh.vertexBuffer.buffer, triangleMesh.vertexBuffer.allocation);
	vmaDestroyBuffer(allocator, monkeyMesh.vertexBuffer.buffer, monkeyMesh.vertexBuffer.allocation);
	vmaDestroyBuffer(allocator, materialBuffer.buffer, materialBuffer.allocation);
	vmaDestroyImage(allocator, lostEmpire.image.image, lostEmpire.image.allocation);
	vmaDestroyImage(allocator, depthImage.image, depthImage.allocation);
	vkDestroyImageView(device, depthImageView, nullptr);
	vkDestroyShaderModule(device, vertexShaderModule, nullptr);
	vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyPipeline(device, graphicsPipeline, nullptr);
	for (int i = 0; i < frame_overlap; i++)
	{
		vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
		vkDestroySemaphore(device, frames[i].renderSemaphore, nullptr);
		vkDestroySemaphore(device, frames[i].presentSemaphore, nullptr);
		vkDestroyFence(device, frames[i].renderFence, nullptr);
		vmaDestroyBuffer(allocator, frames[i].cameraBuffer.buffer, frames[i].cameraBuffer.allocation);
		vmaDestroyBuffer(allocator, frames[i].objectBuffer.buffer, frames[i].objectBuffer.allocation);
	}
	vmaDestroyBuffer(allocator, sceneParameterBuffer.buffer, sceneParameterBuffer.allocation);
	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(device, objectSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, globalSetLayout, nullptr);
	vmaDestroyAllocator(allocator);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);
	for (int i = 0; i < framebuffers.size(); i++)
	{
		vkDestroyFramebuffer(device, framebuffers[i], nullptr);
		vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	}
	vkDestroyDevice(device, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkb::destroy_debug_utils_messenger(instance, debugMessenger);
	vkDestroyInstance(instance, nullptr);

	glfwDestroyWindow(window);
}


#endif