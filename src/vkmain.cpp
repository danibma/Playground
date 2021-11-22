#if 0

#include <assert.h>
#include <stdio.h>

#define GLFW_EXPOSE_NATIVE_WIN32

#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>
#include <vulkan/vulkan.h>

#include <vector>

#define WIDTH 1280
#define HEIGHT 720

#define VK_CHECK(call) \
	do { \
		VkResult result_ = call; \
		assert(result_ == VK_SUCCESS); \
	} while(0)

VkInstance CreateInstance()
{
	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.apiVersion = VK_API_VERSION_1_2;

	VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instanceCreateInfo.pApplicationInfo = &appInfo;

#ifdef _DEBUG
	const char* debugLayers[] =
	{
		"VK_LAYER_KHRONOS_validation"
	};

	instanceCreateInfo.ppEnabledLayerNames = debugLayers;
	instanceCreateInfo.enabledLayerCount = sizeof(debugLayers) / sizeof(debugLayers[0]);
#endif

	const char* extensions[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
	};

	instanceCreateInfo.ppEnabledExtensionNames = extensions;
	instanceCreateInfo.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);

	VkInstance instance = 0;
	VK_CHECK(vkCreateInstance(&instanceCreateInfo, 0, &instance));

	return instance;
}

uint32_t GetGraphicsQueueFamily(VkPhysicalDevice physicalDevice)
{
	VkQueueFamilyProperties queueProps[64];
	uint32_t queueFamilyPropsCount = sizeof(queueProps) / sizeof(queueProps[0]);

	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyPropsCount, queueProps);

	for (uint32_t i = 0; i < queueFamilyPropsCount; i++)
	{
		if (queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			return i;
	}

	//NOTE: this can be used in PickPhysicalDevice to pick rasterezation-capable device
	assert(!"No queue families support graphics");

	return 0;
}

VkPhysicalDevice PickPhysicalDevice(VkPhysicalDevice* physicalDevices, uint32_t physicalDevicesCount)
{
	for (uint32_t i = 0; i < physicalDevicesCount; i++)
	{
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevices[i], &deviceProperties);

		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			printf("Discrete GPU: %s\n", deviceProperties.deviceName);
			return physicalDevices[i];
		}
	}

	if (physicalDevicesCount > 0)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physicalDevices[0], &props);

		printf("Picking fallback GPU: %s\n", props.deviceName);
		return physicalDevices[0];
	}

	printf("No Physical Devices found\n");
	return 0;
}

VkPhysicalDevice CreatePhysicalDevice(VkInstance& instance)
{
	VkPhysicalDevice physicalDevices[16];
	uint32_t physicalDevicesCount = sizeof(physicalDevices) / sizeof(physicalDevices[0]);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDevicesCount, physicalDevices));

	VkPhysicalDevice physicalDevice = PickPhysicalDevice(physicalDevices, physicalDevicesCount);
	assert(physicalDevice);

	return physicalDevice;
}

VkDevice CreateDevice(VkInstance& instance, VkPhysicalDevice& physicalDevice, uint32_t familyIndex)
{
	float queueProps[] = { 1.0f };

	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = familyIndex; // TODO: This needs to be computed from queue properties
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = queueProps;

	const char* extensions[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &queueInfo;
	deviceCreateInfo.ppEnabledExtensionNames = extensions;
	deviceCreateInfo.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);

	VkDevice device = 0;
	VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, 0, &device));

	return device;
}

VkSurfaceKHR CreateSurface(GLFWwindow* window, VkInstance instance)
{
	VkSurfaceKHR surface = 0;
#ifdef VK_USE_PLATFORM_WIN32_KHR
	VkWin32SurfaceCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
	createInfo.hinstance = GetModuleHandle(0);
	createInfo.hwnd = glfwGetWin32Window(window);

	VK_CHECK(vkCreateWin32SurfaceKHR(instance, &createInfo, 0, &surface));
#else 
#error Unsupported platform
#endif
	return surface;
}

VkFormat GetSwapchainFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
	VkSurfaceFormatKHR formats[16];
	uint32_t formatCount = sizeof(formats) / sizeof(formats[0]);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats);

	return formats[0].format;
}

VkSwapchainKHR CreateSwapChain(VkDevice& device, VkSurfaceKHR& surface, uint32_t familyIndex, VkFormat format)
{
	VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	createInfo.surface = surface;
	createInfo.minImageCount = 2; // NOTE: Double buffer
	createInfo.imageFormat = format;
	createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	createInfo.imageExtent.width = WIDTH;
	createInfo.imageExtent.height = HEIGHT;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.queueFamilyIndexCount = 1;
	createInfo.pQueueFamilyIndices = &familyIndex;
	createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	VkSwapchainKHR swapChain = 0;
	VK_CHECK(vkCreateSwapchainKHR(device, &createInfo, 0, &swapChain));

	return swapChain;
}

VkSemaphore CreateSemaphore(VkDevice& device)
{
	VkSemaphoreCreateInfo createInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkSemaphore semaphore = 0;
	VK_CHECK(vkCreateSemaphore(device, &createInfo, 0, &semaphore));

	return semaphore;
}

VkCommandPool CreateCommandPool(VkDevice& device, uint32_t familyIndex)
{
	VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	createInfo.queueFamilyIndex = familyIndex;
	createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

	VkCommandPool commandPool = 0;
	VK_CHECK(vkCreateCommandPool(device, &createInfo, 0, &commandPool));

	return commandPool;
}

VkRenderPass CreateRenderPass(VkDevice device, VkFormat format)
{
	VkAttachmentReference colorAttachment = {};
	colorAttachment.attachment = 0;
	colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachment;


	VkAttachmentDescription attachment[1] = {};
	attachment[0].format = format;
	attachment[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachment[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachment[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;


	VkRenderPassCreateInfo createInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	createInfo.attachmentCount = 1;
	createInfo.pAttachments = attachment;
	createInfo.subpassCount = 1;
	createInfo.pSubpasses = &subpass;


	VkRenderPass renderPass = 0;
	VK_CHECK(vkCreateRenderPass(device, &createInfo, 0, &renderPass));

	return renderPass;
}

VkFramebuffer CreateFramebuffer(VkDevice device, VkRenderPass renderPass, VkImageView imageView)
{
	VkFramebufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	createInfo.renderPass = renderPass;
	createInfo.attachmentCount = 1;
	createInfo.pAttachments = &imageView;
	createInfo.width = WIDTH;
	createInfo.height = HEIGHT;
	createInfo.layers = 1;

	VkFramebuffer framebuffer;
	VK_CHECK(vkCreateFramebuffer(device, &createInfo, 0, &framebuffer));

	return framebuffer;
}

VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format)
{
	VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	createInfo.image = image;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format = format;
	createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	createInfo.subresourceRange.layerCount = 1;
	createInfo.subresourceRange.levelCount = 1;

	VkImageView view = 0;
	vkCreateImageView(device, &createInfo, 0, &view);

	return view;
}

VkShaderModule LoadShader(VkDevice device, const char* path)
{
	FILE* file = fopen(path, "rb");
	assert(file);

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	assert(length >= 0);
	fseek(file, 0, SEEK_SET);

	char* buffer = new char[length];
	assert(buffer);

	size_t rc = fread(buffer, 1, length, file);
	assert(rc == size_t(length));

	fclose(file);

	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = length;
	createInfo.pCode = (const uint32_t*)buffer;

	VkShaderModule shaderModule = 0;
	VK_CHECK(vkCreateShaderModule(device, &createInfo, 0, &shaderModule));

	return shaderModule;
}

VkPipelineLayout CreatePipelineLayout(VkDevice device)
{
	VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

	VkPipelineLayout layout = 0;
	VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, 0, &layout));

	return layout;
}

VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache cache, VkShaderModule VS, VkShaderModule FS, VkRenderPass renderPass, VkPipelineLayout layout)
{
	VkPipelineShaderStageCreateInfo stages[2] = { };
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].pName = "main";
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = VS;
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = FS;

	VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportState.scissorCount = 1;
	viewportState.viewportCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizationState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizationState.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };


	VkPipelineColorBlendAttachmentState colorAttachmentState = { };
	colorAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = &colorAttachmentState;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]);
	dynamicState.pDynamicStates = dynamicStates;


	VkGraphicsPipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	createInfo.stageCount = sizeof(stages) / sizeof(stages[0]);
	createInfo.pStages = stages;
	createInfo.pVertexInputState = &vertexInput;
	createInfo.pInputAssemblyState = &inputAssembly;
	createInfo.pViewportState = &viewportState;
	createInfo.pRasterizationState = &rasterizationState;
	createInfo.pMultisampleState = &multisampleState;
	createInfo.pDepthStencilState = &depthStencilState;
	createInfo.pColorBlendState = &colorBlendState;
	createInfo.pDynamicState = &dynamicState;
	createInfo.layout = layout;

	createInfo.renderPass = renderPass;


	VkPipeline pipeline = 0;
	vkCreateGraphicsPipelines(device, cache, 1, &createInfo, 0, &pipeline);

	return pipeline;
}

int main()
{
	//TODO: Log this asserts and make the glfwInit an if
	int rc = glfwInit();
	assert(rc);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	VkInstance instance = CreateInstance();

	VkPhysicalDevice physicalDevice = CreatePhysicalDevice(instance);

	uint32_t familyIndex = GetGraphicsQueueFamily(physicalDevice);
	VkDevice device = CreateDevice(instance, physicalDevice, familyIndex);

	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", 0, 0);
	assert(window);

	VkSurfaceKHR surface = CreateSurface(window, instance);
	assert(surface);

	VkBool32 presentSupported = 0;
	vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex, surface, &presentSupported);

	VkFormat format = GetSwapchainFormat(physicalDevice, surface);
	assert(format);

	VkSwapchainKHR swapChain = CreateSwapChain(device, surface, familyIndex, format);
	assert(swapChain);

	VkSemaphore acquireSemaphore = CreateSemaphore(device);
	assert(acquireSemaphore);

	VkSemaphore releaseSemaphore = CreateSemaphore(device);
	assert(releaseSemaphore);

	uint32_t imageIndex = 0;

	VkRenderPass renderPass = CreateRenderPass(device, format);
	assert(renderPass);

	VkQueue queue = 0;
	vkGetDeviceQueue(device, familyIndex, 0, &queue);

	VkShaderModule triangleVS = LoadShader(device, "src/shaders/vert.spv");
	assert(triangleVS);
	VkShaderModule triangleFS = LoadShader(device, "src/shaders/frag.spv");
	assert(triangleFS);

	//NOTE: This is critical to performance
	VkPipelineCache pipelineCache = 0;

	VkPipelineLayout layout = CreatePipelineLayout(device);

	VkPipeline trianglePipeline = CreateGraphicsPipeline(device, pipelineCache, triangleVS, triangleFS, renderPass, layout);

	//TODO: Maybe put a macro for max images or get the count
	uint32_t swapchainImagesCount;
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapChain, &swapchainImagesCount, 0));
	std::vector<VkImage> swapchainImages(swapchainImagesCount);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapChain, &swapchainImagesCount, swapchainImages.data()));

	VkImageView swachainImageViews[16];
	for (uint32_t i = 0; i < swapchainImagesCount; i++)
	{
		swachainImageViews[i] = CreateImageView(device, swapchainImages[i], format);
		assert(swachainImageViews[i]);
	}

	VkFramebuffer swapchainFramebuffers[16];
	for (uint32_t i = 0; i < swapchainImagesCount; i++)
	{
		swapchainFramebuffers[i] = CreateFramebuffer(device, renderPass, swachainImageViews[i]);
		assert(swapchainFramebuffers[i]);
	}

	VkCommandPool commandPool = CreateCommandPool(device, familyIndex);
	assert(commandPool);

	VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocateInfo.commandPool = commandPool;
	allocateInfo.commandBufferCount = 1;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VkCommandBuffer commandBuffer = 0;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer));

	VkViewport viewport = {};
	viewport.width = WIDTH;
	viewport.height = -HEIGHT;
	viewport.x = 0;
	viewport.y = HEIGHT;

	VkRect2D scissor = {};
	scissor.extent.width = WIDTH;
	scissor.extent.height = HEIGHT;
	scissor.offset.x = 0;
	scissor.offset.y = 0;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		// NOTE: Semaphore is used to know when the GPU stops using the imageIndex
		VK_CHECK(vkAcquireNextImageKHR(device, swapChain, 0, acquireSemaphore, VK_NULL_HANDLE, &imageIndex));

		VK_CHECK(vkResetCommandPool(device, commandPool, 0));

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;


		VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

		VkClearColorValue color = { 0.0f, 0.5f, 0.8f, 1.0f };
		VkClearValue clearValue = {};
		clearValue.color = color;

		VkRenderPassBeginInfo beginPassInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginPassInfo.renderPass = renderPass;
		beginPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
		beginPassInfo.renderArea.extent.width = WIDTH;
		beginPassInfo.renderArea.extent.height = HEIGHT;
		beginPassInfo.clearValueCount = 1;
		beginPassInfo.pClearValues = &clearValue;

		vkCmdBeginRenderPass(commandBuffer, &beginPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		//Draw Calls here

		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);

		vkCmdDraw(commandBuffer, 3, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffer);

		VK_CHECK(vkEndCommandBuffer(commandBuffer));


		VkPipelineStageFlags submitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &acquireSemaphore;
		submitInfo.pWaitDstStageMask = &submitStageMask;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		submitInfo.pSignalSemaphores = &releaseSemaphore;
		submitInfo.signalSemaphoreCount = 1;
		VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pWaitSemaphores = &acquireSemaphore;
		presentInfo.waitSemaphoreCount = 1;
		VK_CHECK(vkQueuePresentKHR(queue, &presentInfo));

		VK_CHECK(vkDeviceWaitIdle(device));
	}

	glfwDestroyWindow(window);

	vkDestroyInstance(instance, 0);

	return 0;
}
#endif