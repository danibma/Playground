#if 1

#include <assert.h>
#include <stdio.h>

#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>

#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>

#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

constexpr int width = 1600;
constexpr int height = 900;

#define DEBUG_UTILS
#define check(call) \
		{ VkResult result_ = call; \
		assert(result_ == VK_SUCCESS); } 

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
VkSemaphore imageAvailableSemaphore;
VkSemaphore renderFinishedSemaphore;


const std::vector<const char*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

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
	check(vkCreateShaderModule(device, &createInfo, 0, &shaderModule));

	return shaderModule;
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

	// Get queue families
	struct QueueFamilyIndices
	{
		uint32_t graphicsFamily = -1;
	};

	QueueFamilyIndices queueFamilyIndices;
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
		swapchainExtent.width = width;
		swapchainExtent.height = height;
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
	swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

	check(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain));

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

	// Compile shaders
	VkShaderModule vertexShaderModule = CompileShader("src/shaders/triangle.vert.glsl", shaderc_vertex_shader, "vs_main", "Vertex Shader");
	VkShaderModule fragmentShaderModule = CompileShader("src/shaders/triangle.frag.glsl", shaderc_fragment_shader, "ps_main", "Fragment Shader");
	
	VkPipelineShaderStageCreateInfo shaderStages[2];
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

	// Pipeline
	// Vertex Input State
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
	vertexInputInfo.pVertexBindingDescriptions = nullptr;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;
	vertexInputInfo.pVertexAttributeDescriptions = nullptr;

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

	// Pipeline layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 0;
	pipelineLayoutInfo.pSetLayouts = nullptr;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;

	check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	// Render pass 
	// TODO: tentar perceber isto dos subpasses e o crl
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

	// Create command pool
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

	check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));

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
		vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers[i]);
		check(vkEndCommandBuffer(commandBuffers[i]));
	}

	// Create semaphores
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore));
	check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore));
}

void Render()
{
	// Get image from swap chain
	uint32_t imageIndex;
	vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, nullptr, &imageIndex); // NOTE: Using UINT64_MAX disables timeout

	// Submit and synchronize queues
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
	VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	check(vkQueueSubmit(graphicsQueue, 1, &submitInfo, nullptr));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapChains[] = { swapchain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	vkQueuePresentKHR(graphicsQueue, &presentInfo);
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
		Render();
	}

	vkDestroyInstance(instance, nullptr);

	glfwDestroyWindow(window);

	return 0;
}

#endif
