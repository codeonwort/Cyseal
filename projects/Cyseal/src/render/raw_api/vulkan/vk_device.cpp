#include "vk_device.h"
#include "core/assertion.h"
#include "util/logging.h"

#include <vulkan/vulkan.h>

#pragma comment(lib, "vulkan-1.lib")

DEFINE_LOG_CATEGORY(LogVulkan);

const std::vector<const char*> REQUIRED_VALIDATION_LAYERS = {
	"VK_LAYER_KHRONOS_validation"
};


VulkanDevice::VulkanDevice()
{
	// #todo-vulkan
}

VulkanDevice::~VulkanDevice()
{
	// #todo-vulkan
}

void VulkanDevice::initialize(const RenderDeviceCreateParams& createParams)
{
	// #todo-vulkan
	//createVkInstance();
	//setupDebugCallback();
	//createSurface();
	//pickPhysicalDevice();
	//createLogicalDevice();
	//createSwapChain();
	//createImageViews();
	//createRenderPass();
	//createCommandPool();
	//createDepthResources();
	//createFramebuffers();
	//create3DModels();
	//createCommandBuffers();
	//createSemaphores();

	CYLOG(LogVulkan, Log, TEXT("Create a VkInstance"));
	{
		if (createParams.enableDebugLayer)
		{
			enableDebugLayer = checkValidationLayerSupport();
			CHECK(enableDebugLayer);
		}

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "CysealEngine"; // #todo-vulkan: Proper app name
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "CysealEngine"; // #todo-vulkan: Proper engine name
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_1;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		std::vector<const char*> extensions;
		getRequiredExtensions(extensions);
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();
		if (enableDebugLayer)
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(REQUIRED_VALIDATION_LAYERS.size());
			createInfo.ppEnabledLayerNames = REQUIRED_VALIDATION_LAYERS.data();
		}

		VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
		CHECK(result == VK_SUCCESS);
	}

}

void VulkanDevice::recreateSwapChain(HWND hwnd, uint32 width, uint32 height)
{
	// #todo-vulkan
}

void VulkanDevice::flushCommandQueue()
{
	// #todo-vulkan
}

bool VulkanDevice::supportsRayTracing()
{
	// #todo-vulkan: vk_nv_ray_tracing
	return false;
}

VertexBuffer* VulkanDevice::createVertexBuffer(void* data, uint32 sizeInBytes, uint32 strideInBytes)
{
	// #todo-vulkan
	return nullptr;
}

IndexBuffer* VulkanDevice::createIndexBuffer(void* data, uint32 sizeInBytes, EPixelFormat format)
{
	// #todo-vulkan
	return nullptr;
}

bool VulkanDevice::checkValidationLayerSupport()
{
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char* layerName : REQUIRED_VALIDATION_LAYERS)
	{
		bool layerFound = false;
		for (const auto& layerProperties : availableLayers)
		{
			if (strcmp(layerName, layerProperties.layerName) == 0)
			{
				layerFound = true;
				break;
			}
		}
		if (!layerFound)
		{
			return false;
		}
	}
	return true;
}

void VulkanDevice::getRequiredExtensions(std::vector<const char*>& extensions)
{
	extensions.clear();

	{
		//uint32_t glfwExtensionCount = 0;
		//const char** glfwExtensions;
		//glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		//extensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
		extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	}

	if (enableDebugLayer)
	{
		extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	}
}
