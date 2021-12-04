#if 1

#include <assert.h>
#include <stdio.h>

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
		VkResult result_ = call; \
		assert(result_ == VK_SUCCESS);

VkInstance instance;

VkDebugUtilsMessengerEXT debugController;

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

void Init() 
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

	// Create device debug
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
}

int main()
{
	int rc = glfwInit();
	assert(rc);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	GLFWwindow* window = glfwCreateWindow(width, height, "Vulkan", 0, 0);
	assert(window);

	Init();

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

	}

	vkDestroyInstance(instance, nullptr);

	glfwDestroyWindow(window);

	return 0;
}

#if 0
// Testing compiling online
std::string vertexShaderSource;
std::ifstream in("src/shaders/vulkan.hlsl", std::ios::in | std::ios::binary);
if (in)
{
	in.seekg(0, std::ios::end);
	vertexShaderSource.resize(in.tellg());
	in.seekg(0, std::ios::beg);
	in.read(&vertexShaderSource[0], vertexShaderSource.size());
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
shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(vertexShaderSource,
	shaderc_vertex_shader,
	"vertex shader hlsl",
	"vs_main",
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

VkShaderModule vsModule = 0;
VK_CHECK(vkCreateShaderModule(device, &createInfo, 0, &vsModule));

vertexShaderSource;
std::ifstream inn("src/shaders/vulkan.hlsl", std::ios::in | std::ios::binary);
if (inn)
{
	inn.seekg(0, std::ios::end);
	vertexShaderSource.resize(inn.tellg());
	inn.seekg(0, std::ios::beg);
	inn.read(&vertexShaderSource[0], vertexShaderSource.size());
}
else
{
	__debugbreak();
}
in.close();

module = compiler.CompileGlslToSpv(vertexShaderSource,
	shaderc_fragment_shader,
	"pixel shader hlsl",
	"ps_main",
	options);
if (module.GetCompilationStatus() != shaderc_compilation_status_success)
{
	std::cout << module.GetErrorMessage() << std::endl;
	__debugbreak();
}

compiledShader = std::vector<uint32_t>(module.cbegin(), module.cend());
createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
createInfo.codeSize = compiledShader.size() * sizeof(uint32_t);
createInfo.pCode = compiledShader.data();

VkShaderModule psModule = 0;
VK_CHECK(vkCreateShaderModule(device, &createInfo, 0, &psModule));
#endif

#endif