#if 1

#include <assert.h>

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

#include <vkBoostrap/VkBootstrap.h>

#include <tinyobjloader/tiny_obj_loader.h>

#define VMA_IMPLEMENTATION
#include "VulkanMemoryAllocator/vk_mem_alloc.h"

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

	static VertexInputDescription GetVertexDescription();
};

struct Mesh
{
	std::vector<Vertex> vertices;
	AllocatedBuffer vertexBuffer;

	bool loadFromObj(const char* file);
};

struct MeshPushConstants
{
	glm::vec4 data;
	glm::mat4 renderMatrix;
};

struct GPUCameraData 
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 viewproj;
};

struct FrameData {
	VkSemaphore presentSemaphore, renderSemaphore;
	VkFence renderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptor;
};

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
VkDescriptorPool descriptorPool;

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

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);

	return description;
}

bool Mesh::loadFromObj(const char* file)
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

	tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, file, nullptr);
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

				//copy it into our vertex
				Vertex new_vert;
				new_vert.position.x = vx;
				new_vert.position.y = vy;
				new_vert.position.z = vz;

				new_vert.normal.x = nx;
				new_vert.normal.y = ny;
				new_vert.normal.z = nz;

				//we are setting the vertex color as the vertex normal. This is just for display purposes
				new_vert.color = new_vert.normal;


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

void Init(GLFWwindow* window)
{
	// Init vulkan core
	vkb::InstanceBuilder instanceBuilder;

	auto instanceResult = instanceBuilder.set_app_name("Vulkan")
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.desire_api_version(1, 1, 0)
		.set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		.set_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
		.set_debug_callback(debugCallback)
		.build();

	vkb::Instance vkbInstance = instanceResult.value();

	instance = vkbInstance.instance;
	debugMessenger = vkbInstance.debug_messenger;

	glfwCreateWindowSurface(instance, window, nullptr, &surface);

	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 1).set_surface(surface).select().value();

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device vkbDevice = deviceBuilder.build().value();

	device = vkbDevice.device;
	chosenGPU = physicalDevice.physical_device;

	graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo vmaAllocatorInfo = {};
	vmaAllocatorInfo.physicalDevice = chosenGPU;
	vmaAllocatorInfo.device = device;
	vmaAllocatorInfo.instance = instance;

	vkCheck(vmaCreateAllocator(&vmaAllocatorInfo, &allocator));

	// Init mesh
	triangleMesh.vertices.resize(3);
	triangleMesh.vertices[0].position = { 0.5f,  0.5f, 0.0f };
	triangleMesh.vertices[1].position = { -0.5f,  0.5f, 0.0f };
	triangleMesh.vertices[2].position = { 0.0f, -0.5f, 0.0f };
	triangleMesh.vertices[0].color = { 1.0f, 0.0f, 0.0f };
	triangleMesh.vertices[1].color = { 0.0f, 1.0f, 0.0f };
	triangleMesh.vertices[2].color = { 0.0f, 0.0f, 1.0f };

	monkeyMesh.loadFromObj("assets/monkey_smooth.obj");

	// Upload mesh
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = monkeyMesh.vertices.size() * sizeof(Vertex);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	vkCheck(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo,
							&monkeyMesh.vertexBuffer.buffer, &monkeyMesh.vertexBuffer.allocation, nullptr));

	void* data;
	vmaMapMemory(allocator, monkeyMesh.vertexBuffer.allocation, &data);
	memcpy(data, monkeyMesh.vertices.data(), monkeyMesh.vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(allocator, monkeyMesh.vertexBuffer.allocation);


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

	// Init descriptors
	// create a descriptor pool that will hold 10 uniform buffers
	std::vector<VkDescriptorPoolSize> sizes = { {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10} };
	/* 
	 * When creating a descriptor pool, you need to specify how many descriptors of each type you will need,
	 * and what’s the maximum number of sets to allocate from it.
	 */

	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.maxSets = 10;
	descriptorPoolInfo.poolSizeCount = sizes.size();
	descriptorPoolInfo.pPoolSizes = sizes.data();

	vkCheck(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

	VkDescriptorSetLayoutBinding cameraBufferBinding;
	cameraBufferBinding.binding = 0;
	cameraBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	cameraBufferBinding.descriptorCount = 1;
	cameraBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
	descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutInfo.bindingCount = 1;
	descriptorSetLayoutInfo.pBindings = &cameraBufferBinding;

	vkCheck(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &globalSetLayout));

	for (int i = 0; i < frame_overlap; i++)
	{
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.pNext = nullptr;

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

		VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorPool;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		descriptorSetAllocInfo.pSetLayouts = &globalSetLayout;

		vkCheck(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &frames[i].globalDescriptor));

		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = frames[i].cameraBuffer.buffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = sizeof(GPUCameraData);

		VkWriteDescriptorSet setWrite = {};
		setWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		setWrite.dstSet = frames[i].globalDescriptor;
		setWrite.dstBinding = 0;
		setWrite.descriptorCount = 1;
		setWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		setWrite.pBufferInfo = &descriptorBufferInfo;

		vkUpdateDescriptorSets(device, 1, &setWrite, 0, nullptr);
	}

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
	rasterizationStateInfo.cullMode = VK_CULL_MODE_NONE;
	rasterizationStateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
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

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &meshConstantRange;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &globalSetLayout;

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

	//make a model view matrix for rendering the object
	//camera position
	glm::vec3 camPos = { 0.f,0.f,-2.f };

	glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
	//camera projection
	glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)width / (float)height, 0.1f, 200.0f);
	projection[1][1] *= -1;
	//model rotation
	glm::mat4 model = glm::translate(glm::mat4{ 1.0f }, glm::vec3(0, 0, -10))
		* glm::rotate(glm::mat4{ 1.0f }, glm::radians(frameNumber * 0.004f), glm::vec3(0, 1, 0));

	//calculate final mesh matrix
	glm::mat4 meshMatrix = projection * view * model;
	
	GPUCameraData cameraData;
	cameraData.projection = projection;
	cameraData.view = view;
	cameraData.viewproj = projection * view;

	void* data;
	vmaMapMemory(allocator, GetCurrentFrame().cameraBuffer.allocation, &data);
	memcpy(data, &cameraData, sizeof(GPUCameraData));
	vmaUnmapMemory(allocator, GetCurrentFrame().cameraBuffer.allocation);

	vkCmdBeginRenderPass(GetCurrentFrame().mainCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(GetCurrentFrame().mainCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

	VkDeviceSize offset = { 0 };
	vkCmdBindVertexBuffers(GetCurrentFrame().mainCommandBuffer, 0, 1, &monkeyMesh.vertexBuffer.buffer, &offset);

	vkCmdBindDescriptorSets(GetCurrentFrame().mainCommandBuffer, 
							VK_PIPELINE_BIND_POINT_GRAPHICS, 
							pipelineLayout, 
							0, 1,
							&GetCurrentFrame().globalDescriptor,
							0, nullptr);


	MeshPushConstants constants;
	constants.renderMatrix = model;

	vkCmdPushConstants(GetCurrentFrame().mainCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

	vkCmdDraw(GetCurrentFrame().mainCommandBuffer, monkeyMesh.vertices.size(), 1, 0, 0);

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

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (glfwGetKey(window, GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window, true);

		Render(window);
	}

	vkDeviceWaitIdle(device);

	vmaDestroyBuffer(allocator, triangleMesh.vertexBuffer.buffer, triangleMesh.vertexBuffer.allocation);
	vmaDestroyBuffer(allocator, monkeyMesh.vertexBuffer.buffer, monkeyMesh.vertexBuffer.allocation);
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
	}
	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
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