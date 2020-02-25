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

VkDevice CreateDevice(VkInstance& instance, VkPhysicalDevice& physicalDevice, uint32_t* familyIndex)
{
	*familyIndex = 0;

	float queueProps[] = { 1.0f };

	//TODO: Do this "vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queuePropertyCount, &queueProps);"

	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = *familyIndex; // TODO: This needs to be computed from queue properties
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
	uint32_t formatCount = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, 0));
	assert(formatCount > 0);

	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()));

	if (formatCount == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
		return VK_FORMAT_R8G8B8A8_UNORM;

	for (uint32_t i = 0; i < formatCount; ++i)
		if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
			return formats[i].format;

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

int main() 
{
	//TODO: Log this asserts and make the glfwInit an if
	int rc = glfwInit();
	assert(rc);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	VkInstance instance = CreateInstance();

	VkPhysicalDevice physicalDevice = CreatePhysicalDevice(instance);

	uint32_t familyIndex = 0;
	VkDevice device = CreateDevice(instance, physicalDevice, &familyIndex);

	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Skel Engine", 0, 0);
	assert(window);

	VkSurfaceKHR surface = CreateSurface(window, instance);
	assert(surface);

	VkFormat format = GetSwapchainFormat(physicalDevice, surface);
	assert(format);

	VkSwapchainKHR swapChain = CreateSwapChain(device, surface, familyIndex, format);
	assert(swapChain);

	VkSemaphore acquireSemaphore = CreateSemaphore(device);
	assert(acquireSemaphore);

	VkSemaphore releaseSemaphore = CreateSemaphore(device);
	assert(releaseSemaphore);

	uint32_t imageIndex = 0;

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapChain;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pWaitSemaphores = &acquireSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	VkQueue queue = 0;
	vkGetDeviceQueue(device, familyIndex, 0, &queue);

	VkImage swapchainImages[16];
	uint32_t swapchainImagesCount = sizeof(swapchainImages) / sizeof(swapchainImages[0]);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapChain, &swapchainImagesCount, swapchainImages));

	VkCommandPool commandPool = CreateCommandPool(device, familyIndex);
	assert(commandPool);

	VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocateInfo.commandPool = commandPool;
	allocateInfo.commandBufferCount = 1;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VkCommandBuffer commandBuffer = 0;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer));

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		// NOTE: Semaphore is used to know when the GPU stops using the imageIndex
		VK_CHECK(vkAcquireNextImageKHR(device, swapChain, 0, acquireSemaphore, VK_NULL_HANDLE, &imageIndex));

		VK_CHECK(vkResetCommandPool(device, commandPool, 0));

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		
		VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.levelCount = 1;
		range.layerCount = 1;

		VkClearColorValue color = { 0.0f, 0.8f, 0.5f, 1.0f };
		vkCmdClearColorImage(commandBuffer, swapchainImages[0], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
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

		VK_CHECK(vkQueuePresentKHR(queue, &presentInfo));

		VK_CHECK(vkDeviceWaitIdle(device));
	}

	glfwDestroyWindow(window);

	vkDestroyInstance(instance, 0);
}