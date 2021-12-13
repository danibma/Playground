#if 0

#include <assert.h>
#include <stdio.h>

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

int m_width = 1600;
int m_height = 900;
constexpr int max_frames_in_flight = 2;

#define DEBUG_UTILS
#define check(call) \
		{ VkResult result_ = call; \
		assert(result_ == VK_SUCCESS); } 

struct QueueFamilyIndices
{
	uint32_t graphicsFamily = -1;
};

struct Vertex
{
	glm::vec2 pos;
	glm::vec3 color;

	static VkVertexInputBindingDescription GetBindingDescription()
	{
		VkVertexInputBindingDescription bindingDescription = {};
		bindingDescription.binding = 0;
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		bindingDescription.stride = sizeof(Vertex);

		return bindingDescription;
	}

	static std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions()
	{
		std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions = {};
		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].offset = offsetof(Vertex, pos);
		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		return attributeDescriptions;
	}

};

VkInstance instance;
VkDebugUtilsMessengerEXT debugController;
VkPhysicalDevice physicalDevice;
VkDevice device;
VkQueue graphicsQueue;
VkSurfaceKHR surface;
std::vector<VkDeviceQueueCreateInfo> deviceQueuesInfos;
VkSwapchainKHR swapchain;
VkExtent2D swapchainExtent = {};
VkSurfaceFormatKHR colorFormat;
std::vector<VkImage> swapchainImages;
uint32_t swapchainImagesCount;
std::vector<VkImageView> swapchainImageViews;
VkPipelineLayout pipelineLayout;
VkRenderPass renderPass;
VkPipeline graphicsPipeline;
std::vector<VkFramebuffer> framebuffers;
VkCommandPool commandPool;
std::vector<VkCommandBuffer> commandBuffers;
std::vector<VkSemaphore> imageAvailableSemaphores;
std::vector<VkSemaphore> renderFinishedSemaphores;
std::vector<VkFence> inFlightFences;
std::vector<VkFence> imagesInFlight;
size_t currentFrame = 0;
QueueFamilyIndices queueFamilyIndices;
VkPipelineShaderStageCreateInfo shaderStages[2];
VkShaderModule vertexShaderModule;
VkShaderModule fragmentShaderModule;
VkBuffer vertexBuffer;
VkDeviceMemory vertexBufferMemory;
VkBuffer indexBuffer;
VkDeviceMemory indexBufferMemory;
VkPhysicalDeviceMemoryProperties memoryProperties;
VkDescriptorSetLayout descriptorSetLayout;
std::vector<VkBuffer> uniformBuffers;
std::vector<VkDeviceMemory> uniformBuffersMemory;

struct MVP
{
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

struct UniformBufferObject
{
	glm::mat4 mvp;
};

const std::vector<Vertex> vertices = {
	{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
	{{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
	{{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
	{{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
	0, 1, 2, 2, 3, 0
};

const std::vector<const char*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

void CreateSwapchain(GLFWwindow* window);
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{

	if (messageSeverity& VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		std::cout << "\033[1;33mWarning: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		std::cerr << "\033[1;31mError: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}
	else
	{
		std::cerr << "\033[1;36mInfo: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}

	return VK_FALSE;
}

uint32_t GetMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
	{
		if (typeFilter & (1 << i))
		{
			if((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				return i;
		}
	}
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
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
	options.SetWarningsAsErrors();
	options.SetGenerateDebugInfo();
	options.SetSourceLanguage(shaderc_source_language_hlsl);
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
	check(vkCreateShaderModule(device, &createInfo, 0, &shaderModule));

	return shaderModule;
}

void OnResize(GLFWwindow* window)
{
	vkDeviceWaitIdle(device);

	CreateSwapchain(window);
}

void CreateSwapchain(GLFWwindow* window)
{
	VkSwapchainKHR oldSwapchain = swapchain;

	// Create swap chain
	VkSurfaceCapabilitiesKHR capabilities;
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync
															 // NOTE: if we dont want vsync, try to find a mailbox mode

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

	uint32_t colorFormatsCount;
	std::vector<VkSurfaceFormatKHR> colorFormats;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &colorFormatsCount, nullptr);
	assert(colorFormatsCount > 0);
	colorFormats.resize(colorFormatsCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &colorFormatsCount, colorFormats.data());

	bool foundBestFormat = false;
	for (const auto& format : colorFormats)
	{
		if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && format.format == VK_FORMAT_B8G8R8A8_SRGB)
		{
			colorFormat = format;
			foundBestFormat = true;
			break;
		}
	}
	if (!foundBestFormat)
	{
		colorFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		colorFormat.format = VK_FORMAT_B8G8R8A8_SRGB;
	}

	if (capabilities.currentExtent.width == -1)
	{
		// if the surface size is undefined use
		// size of window
		swapchainExtent.width = m_width;
		swapchainExtent.height = m_height;
	}
	else
	{
		// if the surface size if defined
		// the swap chain size must match
		swapchainExtent = capabilities.currentExtent;
	}

	uint32_t imagesCount = capabilities.minImageCount + 1;
	if (imagesCount > capabilities.maxImageCount)
		imagesCount = capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchainInfo = {};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	// swapchainInfo.flags
	swapchainInfo.surface = surface;
	swapchainInfo.minImageCount = imagesCount;
	swapchainInfo.imageFormat = colorFormat.format;
	swapchainInfo.imageColorSpace = colorFormat.colorSpace;
	swapchainInfo.imageExtent = swapchainExtent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // It is also possible that you'll render images to a separate image first to perform operations like post-processing. 
																	// In that case you may use a value like VK_IMAGE_USAGE_TRANSFER_DST_BIT instead 
																	// and use a memory operation to transfer the rendered image to a swap chain image.
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainInfo.queueFamilyIndexCount = 0;
	swapchainInfo.pQueueFamilyIndices = NULL;
	swapchainInfo.preTransform = capabilities.currentTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode = presentMode;
	swapchainInfo.clipped = true;
	swapchainInfo.oldSwapchain = oldSwapchain;
	check(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain));

	if (oldSwapchain != VK_NULL_HANDLE)
	{
		for (uint32_t i = 0; i < swapchainImagesCount; i++)
		{
			vkDestroyImageView(device, swapchainImageViews[i], nullptr);
		}
		vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
	}
	// Get Swap Chain Images
	vkGetSwapchainImagesKHR(device, swapchain, &swapchainImagesCount, nullptr);
	swapchainImages.resize(swapchainImagesCount);
	vkGetSwapchainImagesKHR(device, swapchain, &swapchainImagesCount, swapchainImages.data());

	// Create image view
	swapchainImageViews.resize(swapchainImagesCount);
	for (uint32_t i = 0; i < swapchainImages.size(); i++)
	{
		VkImageViewCreateInfo imageViewInfo = {};
		imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewInfo.image = swapchainImages[i];
		imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewInfo.format = colorFormat.format;
		imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewInfo.subresourceRange.baseArrayLayer = 0;
		imageViewInfo.subresourceRange.baseMipLevel = 0;
		imageViewInfo.subresourceRange.layerCount = 1;
		imageViewInfo.subresourceRange.levelCount = 1;

		check(vkCreateImageView(device, &imageViewInfo, nullptr, &swapchainImageViews[i]));
	}

	// Pipeline
	// Create command pool
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

	check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));

	//Create staging vertex buffer
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	VkBufferCreateInfo stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.size = sizeof(vertices[0]) * vertices.size();
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	check(vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer));

	VkMemoryRequirements stagingBufferMemoryRequirements;
	vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingBufferMemoryRequirements);

	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = stagingBufferMemoryRequirements.size;
	allocateInfo.memoryTypeIndex = GetMemoryType(stagingBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	check(vkAllocateMemory(device, &allocateInfo, nullptr, &stagingBufferMemory));
	vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);

	void* data;
	vkMapMemory(device, stagingBufferMemory, 0, stagingBufferInfo.size, 0, &data);
	memcpy(data, vertices.data(), stagingBufferInfo.size);
	vkUnmapMemory(device, stagingBufferMemory);

	// Create vertex buffer
	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.size = sizeof(vertices[0]) * vertices.size();
	vertexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	check(vkCreateBuffer(device, &vertexBufferInfo, nullptr, &vertexBuffer));

	// Assign memory to vertex buffer
	VkMemoryRequirements vertexBufferMemoryRequirements;
	vkGetBufferMemoryRequirements(device, vertexBuffer, &vertexBufferMemoryRequirements);

	allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = vertexBufferMemoryRequirements.size;
	allocateInfo.memoryTypeIndex = GetMemoryType(vertexBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	check(vkAllocateMemory(device, &allocateInfo, nullptr, &vertexBufferMemory));
	vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

	// Copy memory from staging buffer to vertex buffer
	VkCommandBufferAllocateInfo tempBufferAllocateInfo = {};
	tempBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	tempBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	tempBufferAllocateInfo.commandPool = commandPool;
	tempBufferAllocateInfo.commandBufferCount = 1;

	VkCommandBuffer tempCommandBuffer;
	vkAllocateCommandBuffers(device, &tempBufferAllocateInfo, &tempCommandBuffer);

	VkCommandBufferBeginInfo tempCommandBufferBeginInfo{};
	tempCommandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	tempCommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(tempCommandBuffer, &tempCommandBufferBeginInfo);

	VkBufferCopy bufferCopy = {};
	bufferCopy.size = sizeof(vertices[0]) * vertices.size();
	vkCmdCopyBuffer(tempCommandBuffer, stagingBuffer, vertexBuffer, 1, &bufferCopy);

	vkEndCommandBuffer(tempCommandBuffer);

	VkSubmitInfo tempBufferSubmitInfo = {};
	tempBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	tempBufferSubmitInfo.commandBufferCount = 1;
	tempBufferSubmitInfo.pCommandBuffers = &tempCommandBuffer;

	vkQueueSubmit(graphicsQueue, 1, &tempBufferSubmitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue);

	vkFreeCommandBuffers(device, commandPool, 1, &tempCommandBuffer);
	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);

	// Create staging index buffer
	stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.size = sizeof(indices[0]) * indices.size();
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	check(vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer));

	vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingBufferMemoryRequirements);

	allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = stagingBufferMemoryRequirements.size;
	allocateInfo.memoryTypeIndex = GetMemoryType(stagingBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	check(vkAllocateMemory(device, &allocateInfo, nullptr, &stagingBufferMemory));
	vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);

	vkMapMemory(device, stagingBufferMemory, 0, stagingBufferInfo.size, 0, &data);
	memcpy(data, indices.data(), stagingBufferInfo.size);
	vkUnmapMemory(device, stagingBufferMemory);

	// Create index buffer
	VkBufferCreateInfo indexBufferInfo = {};
	indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexBufferInfo.size = sizeof(indices[0]) * indices.size();
	indexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	check(vkCreateBuffer(device, &indexBufferInfo, nullptr, &indexBuffer));

	// Assign memory to index buffer
	VkMemoryRequirements indexBufferMemoryRequirements;
	vkGetBufferMemoryRequirements(device, indexBuffer, &indexBufferMemoryRequirements);

	allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = indexBufferMemoryRequirements.size;
	allocateInfo.memoryTypeIndex = GetMemoryType(indexBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	check(vkAllocateMemory(device, &allocateInfo, nullptr, &indexBufferMemory));
	vkBindBufferMemory(device, indexBuffer, indexBufferMemory, 0);

	// Copy memory from index staging buffer to index buffer
	tempBufferAllocateInfo = {};
	tempBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	tempBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	tempBufferAllocateInfo.commandPool = commandPool;
	tempBufferAllocateInfo.commandBufferCount = 1;

	vkAllocateCommandBuffers(device, &tempBufferAllocateInfo, &tempCommandBuffer);

	tempCommandBufferBeginInfo = {};
	tempCommandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	tempCommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(tempCommandBuffer, &tempCommandBufferBeginInfo);

	bufferCopy = {};
	bufferCopy.size = sizeof(indices[0]) * indices.size();
	vkCmdCopyBuffer(tempCommandBuffer, stagingBuffer, indexBuffer, 1, &bufferCopy);

	vkEndCommandBuffer(tempCommandBuffer);

	tempBufferSubmitInfo = {};
	tempBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	tempBufferSubmitInfo.commandBufferCount = 1;
	tempBufferSubmitInfo.pCommandBuffers = &tempCommandBuffer;

	vkQueueSubmit(graphicsQueue, 1, &tempBufferSubmitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue);

	vkFreeCommandBuffers(device, commandPool, 1, &tempCommandBuffer);
	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);

	// Vertex Input State
	auto bindingDescription = Vertex::GetBindingDescription();
	auto attributeDescriptions = Vertex::GetAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	// Input Assembly
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyInfo.primitiveRestartEnable = false;

	// Viewport State
	VkViewport viewport = {};
	viewport.width = swapchainExtent.width;
	viewport.height = swapchainExtent.height;
	viewport.x = 0;
	viewport.y = 0;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportStateInfo = {};
	viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.pViewports = &viewport;
	viewportStateInfo.scissorCount = 1;
	viewportStateInfo.pScissors = &scissor;

	// Rasterizer
	VkPipelineRasterizationStateCreateInfo rasterizerStateInfo = {};
	rasterizerStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizerStateInfo.depthClampEnable = false;
	rasterizerStateInfo.rasterizerDiscardEnable = false;
	rasterizerStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizerStateInfo.cullMode = VK_CULL_MODE_NONE;
	rasterizerStateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizerStateInfo.lineWidth = 1.0f;
	rasterizerStateInfo.depthBiasEnable = false;
	rasterizerStateInfo.depthBiasConstantFactor = 0.0f;
	rasterizerStateInfo.depthBiasClamp = 0.0f;
	rasterizerStateInfo.depthBiasSlopeFactor = 0.0f;

	// Multisampling
	VkPipelineMultisampleStateCreateInfo multisamplingStateInfo = {};
	multisamplingStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisamplingStateInfo.sampleShadingEnable = VK_FALSE;
	multisamplingStateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisamplingStateInfo.minSampleShading = 1.0f;
	multisamplingStateInfo.pSampleMask = nullptr;
	multisamplingStateInfo.alphaToCoverageEnable = VK_FALSE;
	multisamplingStateInfo.alphaToOneEnable = VK_FALSE;

	// Color Blend
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {};
	colorBlendStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateInfo.logicOpEnable = false;
	colorBlendStateInfo.attachmentCount = 1;
	colorBlendStateInfo.pAttachments = &colorBlendAttachment;

	// create descriptor set layout
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
	descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutInfo.bindingCount = 1;
	descriptorSetLayoutInfo.pBindings = &uboLayoutBinding;

	check(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout));

	// Pipeline layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;

	check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	// Create uniform buffers
	VkDeviceSize uniformBufferSize = sizeof(UniformBufferObject);

	uniformBuffers.resize(swapchainImages.size());
	uniformBuffersMemory.resize(swapchainImages.size());

	for (uint32_t i = 0; i < swapchainImages.size(); i++)
	{
		VkBufferCreateInfo uniformBufferInfo = {};
		uniformBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		uniformBufferInfo.size = sizeof(indices[0]) * indices.size();
		uniformBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		uniformBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		check(vkCreateBuffer(device, &uniformBufferInfo, nullptr, &uniformBuffers[i]));

		// Assign memory to index buffer
		VkMemoryRequirements uniformBufferMemoryRequirements;
		vkGetBufferMemoryRequirements(device, uniformBuffers[i], &uniformBufferMemoryRequirements);

		allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = uniformBufferMemoryRequirements.size;
		allocateInfo.memoryTypeIndex = GetMemoryType(uniformBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		check(vkAllocateMemory(device, &allocateInfo, nullptr, &uniformBuffersMemory[i]));
		vkBindBufferMemory(device, uniformBuffers[i], uniformBuffersMemory[i], 0);
	}

	// Render pass 
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = colorFormat.format;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription colorSubpass = {};
	colorSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	colorSubpass.colorAttachmentCount = 1;
	colorSubpass.pColorAttachments = &colorAttachmentRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &colorSubpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	check(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));

	// Create graphics pipeline
	VkGraphicsPipelineCreateInfo graphicsPipelineInfo = {};
	graphicsPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	graphicsPipelineInfo.stageCount = 2;
	graphicsPipelineInfo.pStages = shaderStages;
	graphicsPipelineInfo.pVertexInputState = &vertexInputInfo;
	graphicsPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	graphicsPipelineInfo.pTessellationState = nullptr;
	graphicsPipelineInfo.pViewportState = &viewportStateInfo;
	graphicsPipelineInfo.pRasterizationState = &rasterizerStateInfo;
	graphicsPipelineInfo.pMultisampleState = &multisamplingStateInfo;
	graphicsPipelineInfo.pDepthStencilState = nullptr;
	graphicsPipelineInfo.pColorBlendState = &colorBlendStateInfo;
	graphicsPipelineInfo.pDynamicState = nullptr;
	graphicsPipelineInfo.layout = pipelineLayout;
	graphicsPipelineInfo.renderPass = renderPass;
	graphicsPipelineInfo.subpass = 0;

	check(vkCreateGraphicsPipelines(device, 0, 1, &graphicsPipelineInfo, nullptr, &graphicsPipeline));

	// Create framebuffers
	framebuffers.resize(swapchainImageViews.size());
	for (uint32_t i = 0; i < swapchainImageViews.size(); i++)
	{
		auto imageView = swapchainImageViews[i];

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &imageView;
		framebufferInfo.width = swapchainExtent.width;
		framebufferInfo.height = swapchainExtent.height;
		framebufferInfo.layers = 1;

		check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]));
	}

	// Create command buffer
	commandBuffers.resize(framebuffers.size());
	VkCommandBufferAllocateInfo commandBufferAllocationInfo = {};
	commandBufferAllocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocationInfo.commandPool = commandPool;
	commandBufferAllocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocationInfo.commandBufferCount = commandBuffers.size();

	check(vkAllocateCommandBuffers(device, &commandBufferAllocationInfo, commandBuffers.data()));

	for (size_t i = 0; i < commandBuffers.size(); i++)
	{
		VkCommandBufferBeginInfo commandbufferBeginInfo = {};
		commandbufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		commandbufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		check(vkBeginCommandBuffer(commandBuffers[i], &commandbufferBeginInfo));

		VkClearValue clearColor = { {{0.0f, 0.2f, 1.0f, 1.0f}} };
		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.framebuffer = framebuffers[i];
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = &clearColor;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent = swapchainExtent;
		renderPassBeginInfo.renderArea.offset = { 0, 0 };

		vkCmdBeginRenderPass(commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

		VkDeviceSize offset[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &vertexBuffer, offset);

		vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT16);

		vkCmdDrawIndexed(commandBuffers[i], indices.size(), 1, 0, 0, 0);

		vkCmdEndRenderPass(commandBuffers[i]);
		check(vkEndCommandBuffer(commandBuffers[i]));
	}

	// Create semaphores
	imageAvailableSemaphores.resize(max_frames_in_flight);
	renderFinishedSemaphores.resize(max_frames_in_flight);
	inFlightFences.resize(max_frames_in_flight);
	imagesInFlight.resize(swapchainImages.size(), NULL);
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < max_frames_in_flight; i++)
	{
		check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]));
		check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]));
		check(vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]));
	}
}

void Init(GLFWwindow* window) 
{
	// Create Instance
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.apiVersion = VK_API_VERSION_1_2;
	appInfo.pApplicationName = "Vulkan";
	appInfo.pEngineName = "No Engine";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

	std::vector<const char*> extensionNames = { "VK_KHR_surface", "VK_KHR_win32_surface" };
	if (enableValidationLayers)
	{
		extensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		extensionNames.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	}

	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledExtensionCount = extensionNames.size();
	instanceInfo.ppEnabledExtensionNames = extensionNames.data();

	// Check validation layer support
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char* layerName : validationLayers) 
	{
		bool layerFound = false;

		std::cout << "Vulkan Instance Layers:" << std::endl;
		for (const auto& layerProperties : availableLayers) 
		{
			std::cout << "    " << layerProperties.layerName << std::endl;
			if (strcmp(layerName, layerProperties.layerName) == 0) 
			{
				layerFound = true;
				break;
			}
		}

		if (!layerFound) 
		{
			std::runtime_error("validation layers requested, but not available!");
		}
	}

	if (enableValidationLayers) 
	{
		instanceInfo.enabledLayerCount = validationLayers.size();
		instanceInfo.ppEnabledLayerNames = validationLayers.data();
	}
	else
	{
		instanceInfo.enabledLayerCount = 0;
	}

	check(vkCreateInstance(&instanceInfo, nullptr, &instance));

	// Create debug controller
	if (enableValidationLayers) 
	{
		VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
		debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugInfo.pfnUserCallback = debugCallback;


		auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		assert(vkCreateDebugUtilsMessengerEXT != NULL);
		check(vkCreateDebugUtilsMessengerEXT(instance, &debugInfo, nullptr, &debugController));
	}
	
	// Create window surface
	check(glfwCreateWindowSurface(instance, window, nullptr, &surface));

	// Pick physical device
	uint32_t gpuCount = 0;
	vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);

	std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
	vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());

	VkPhysicalDevice selectedPhysicalDevice = nullptr;
	VkPhysicalDeviceProperties properties;
	for (VkPhysicalDevice device : physicalDevices)
	{
		vkGetPhysicalDeviceProperties(device, &properties);
		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			selectedPhysicalDevice = device;
			break;
		}
	}
	assert(selectedPhysicalDevice);

	physicalDevice = selectedPhysicalDevice;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	// Get queue families
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

	{
		uint32_t i = 0;
		VkBool32 presentSupported = false;
		for (const VkQueueFamilyProperties& queueFamily : queueFamilyProperties)
		{
			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				// Check if the current graphics queue supports present too
				check(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupported));
				if (presentSupported)
					queueFamilyIndices.graphicsFamily = i;
			}
			i++;
		}

		// TODO: in the future maybe add an option so it tries to find a present queue and a graphics queue
		// In case they are not the same
		assert(presentSupported);
	}

	// TODO: In case of more queuefamilies we create a vector with all this infos
	static constexpr float defaultQueuePriority = 0.0f;
	VkDeviceQueueCreateInfo graphicsQueueInfo = {};
	graphicsQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	graphicsQueueInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
	graphicsQueueInfo.queueCount = 1;
	graphicsQueueInfo.pQueuePriorities = &defaultQueuePriority;
	deviceQueuesInfos.push_back(graphicsQueueInfo);

	// Create device
	uint32_t deviceExtensionsCount;
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionsCount, nullptr);
	std::vector<VkExtensionProperties> deviceExtensionsSupported(deviceExtensionsCount);
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionsCount, deviceExtensionsSupported.data());

	bool swapChainSupported = false;
	std::vector<const char*> deviceExtensions;
	std::cout << "This device supports " << deviceExtensionsSupported.size() << " extensions: " << std::endl;
	// TODO: in the future create a function like IsExtensionSupported(extensionName)
	for (const auto& extension : deviceExtensionsSupported)
	{
		std::cout << "    " << extension.extensionName << std::endl;
		if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
		{
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			swapChainSupported = true;
		}
	}

	assert(swapChainSupported);

	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = deviceQueuesInfos.size(); // queue familes count
	deviceInfo.pQueueCreateInfos = deviceQueuesInfos.data();
	deviceInfo.enabledExtensionCount = deviceExtensions.size();
	deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
	deviceInfo.pEnabledFeatures = nullptr;

	check(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device));

	// Get graphics queue
	vkGetDeviceQueue(device, queueFamilyIndices.graphicsFamily, 0, &graphicsQueue);

	// Compile shaders
	vertexShaderModule = CompileShader("src/shaders/vulkan.hlsl", shaderc_vertex_shader, "vs_main", "Vertex Shader");
	fragmentShaderModule = CompileShader("src/shaders/vulkan.hlsl", shaderc_fragment_shader, "ps_main", "Fragment Shader");
	
	VkPipelineShaderStageCreateInfo vertexShaderStageInfo = {};
	vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertexShaderStageInfo.module = vertexShaderModule;
	vertexShaderStageInfo.pName = "vs_main";
	shaderStages[0] = vertexShaderStageInfo;

	VkPipelineShaderStageCreateInfo fragmentShaderStageInfo = {};
	fragmentShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragmentShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragmentShaderStageInfo.module = fragmentShaderModule;
	fragmentShaderStageInfo.pName = "ps_main";
	shaderStages[1] = fragmentShaderStageInfo;

	// Create swapchain
	CreateSwapchain(window);
}

void Render(GLFWwindow* window)
{
	vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

	// Get image from swap chain
	uint32_t imageIndex;
	auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], nullptr, &imageIndex); // NOTE: Using UINT64_MAX disables timeout
	if (result != VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
	{
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			// Swap chain is no longer compatible with the surface and needs to be recreated
			OnResize(window);
			return;
		}
		else
		{
			check(result);
		}
	}

	 // Check if a previous frame is using this image (i.e. there is its fence to wait on)
	if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
	{
		vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
	}
	// Mark the image as now being in use by this frame
	imagesInFlight[imageIndex] = inFlightFences[currentFrame];

	// Update uniform buffer
	// TODO: check "push constants"
	static auto startTime = std::chrono::high_resolution_clock::now();
	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	MVP mvp = {};
	mvp.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	mvp.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	mvp.proj = glm::perspective(glm::radians(45.0f), swapchainExtent.width / (float)swapchainExtent.height, 0.1f, 10.0f);
	mvp.proj[1][1] *= -1; // TOOD: change this to use LH

	UniformBufferObject ubo = { mvp.proj * mvp.view * mvp.model };

	void* data;
	vkMapMemory(device, uniformBuffersMemory[currentFrame], 0, sizeof(ubo), 0, &data);
	memcpy(data, &ubo, sizeof(ubo));
	vkUnmapMemory(device, uniformBuffersMemory[currentFrame]);

	// Submit and synchronize queues
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
	VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	vkResetFences(device, 1, &inFlightFences[currentFrame]);
	check(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapChains[] = { swapchain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	result = vkQueuePresentKHR(graphicsQueue, &presentInfo);
	if (result != VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
	{
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			// Swap chain is no longer compatible with the surface and needs to be recreated
			OnResize(window);
			return;
		}
		else
		{
			check(result);
		}
	}

	check(vkQueueWaitIdle(graphicsQueue));

	currentFrame = (currentFrame + 1) % max_frames_in_flight;
}

int main()
{
	int rc = glfwInit();
	assert(rc);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWwindow* window = glfwCreateWindow(m_width, m_height, "Vulkan", 0, 0);
	assert(window);

	// Set GLFW callbacks
	glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width, int height)
	{
		m_width = width;
		m_height = height;
		OnResize(window);
	});

	Init(window);

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (glfwGetKey(window, GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window, true);

		Render(window);
	}

	vkWaitForFences(device, imagesInFlight.size(), imagesInFlight.data(), true, UINT64_MAX);
	vkDeviceWaitIdle(device);

	// Destroy created objects
	for(auto& imageAvailableSemaphore : imageAvailableSemaphores)
		vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
	for (auto& renderFinishedSemaphore : renderFinishedSemaphores)
		vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
	for (auto& inFlightFence : inFlightFences)
		vkDestroyFence(device, inFlightFence, nullptr);
	for(auto& framebuffer : framebuffers)
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	for (auto& imageView : swapchainImageViews)
		vkDestroyImageView(device, imageView, nullptr);
	vkFreeCommandBuffers(device, commandPool, commandBuffers.size(), commandBuffers.data());
	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);
	vkDestroyPipeline(device, graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyShaderModule(device, vertexShaderModule, nullptr);
	vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroyBuffer(device, vertexBuffer, nullptr);
	vkDestroyBuffer(device, indexBuffer, nullptr);
	vkFreeMemory(device, vertexBufferMemory, nullptr);
	vkFreeMemory(device, indexBufferMemory, nullptr);
	vkDestroyDevice(device, nullptr);
	if (enableValidationLayers)
	{
		auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
		assert(vkDestroyDebugUtilsMessengerEXT != NULL);
		vkDestroyDebugUtilsMessengerEXT(instance, debugController, nullptr);
	}
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);

	glfwDestroyWindow(window);

	return 0;
}

#endif
