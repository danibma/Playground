#include <assert.h>
#include <stdio.h>

#include <glfw/glfw3.h>
#include <vulkan/vulkan.h>

#define WIDTH 1280
#define HEIGHT 720

#define VK_CHECK(call) \
	do { \
		VkResult result_ = call; \
		assert(result_ == VK_SUCCESS); \
	} while(0)

int main() 
{
	//TODO: Log this asserts and make the glfwInit an if
	int rc = glfwInit();
	assert(rc);

	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.apiVersion = VK_API_VERSION_1_2;

	VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instanceCreateInfo.pApplicationInfo = &appInfo;

#ifdef _DEBUG
	const char* debugLayers[] = 
	{
		"VK_LAYER_LUNARG_standard_validation"
	};

	instanceCreateInfo.ppEnabledLayerNames = debugLayers;
	instanceCreateInfo.enabledLayerCount = sizeof(debugLayers) / sizeof(debugLayers[0]);
#endif

	const char* extensions[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME
	};

	instanceCreateInfo.ppEnabledExtensionNames = extensions;
	instanceCreateInfo.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);

	VkInstance instance = 0;
	VK_CHECK(vkCreateInstance(&instanceCreateInfo, 0, &instance));


	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Skel Engine", 0, 0);
	assert(window);

	while (!glfwWindowShouldClose(window))
	{
		
		glfwPollEvents();
	}

	glfwDestroyWindow(window);

	vkDestroyInstance(instance, 0);
}