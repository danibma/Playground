#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Renderer.h"

#include <vkBoostrap/VkBootstrap.h>

#include <tinyobjloader/tiny_obj_loader.h>

#include <iostream>

// Variables
uint32_t width = 1600, height = 900;

VkInstance instance;
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
VkCommandPool commandPool;
std::vector<VkCommandBuffer> commandBuffers;

int main()
{
	// Init window
	int rc = glfwInit();
	assert(rc);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	GLFWwindow* window = glfwCreateWindow(width, height, "Vulkan Cookbook", 0, 0);
	assert(window);


	// Init vulkan core
	vkb::InstanceBuilder instanceBuilder;

	auto instanceResult = instanceBuilder.set_app_name("Vulkan Cookbook")
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

	vkCheck(vmaCreateAllocator(&vmaAllocatorInfo, &Renderer::allocator));

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

	// Create Command pool
	Renderer::CreateCommandPool(device, 0, graphicsQueueFamily, commandPool);
	
	// Create command buffers
	Renderer::AllocateCommandBuffers(device, commandPool, 1, commandBuffers);

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (glfwGetKey(window, GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window, true);
	}

	glfwDestroyWindow(window);

	return 0;
}