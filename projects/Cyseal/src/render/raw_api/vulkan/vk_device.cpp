#include "vk_device.h"
#include "core/platform.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_render_command.h"
#include "vk_swapchain.h"
#include "vk_texture.h"
#include "vk_buffer.h"
#include "vk_shader.h"
#include "vk_utils.h"
#include "core/platform.h"
#include "core/assertion.h"
#include "render/swap_chain.h"

#include <algorithm>
#include <limits>
#include <string>
#include <array>
#include <set>

#include <vulkan/vulkan.h>

#if PLATFORM_WINDOWS
	#include "vk_win32.h"
#endif

// #todo-crossapi: Dynamic loading
#pragma comment(lib, "vulkan-1.lib")

#define VK_APPINFO_APPNAME       "CysealApplication"
#define VK_APPINFO_APPVER        VK_MAKE_API_VERSION(0, 1, 0, 0)
#define VK_APPINFO_ENGINENAME    "CysealEngine"
#define VK_APPINFO_ENGINEVER     VK_MAKE_API_VERSION(0, 1, 0, 0)
#define VK_MAX_API               VK_API_VERSION_1_3

DEFINE_LOG_CATEGORY(LogVulkan);

const std::vector<const char*> REQUIRED_VALIDATION_LAYERS = {
	"VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> REQUIRED_DEVICE_EXTENSIONS = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#include <iostream>
static VKAPI_ATTR VkBool32 VKAPI_CALL GVulkanDebugCallback(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objType,
	uint64_t obj,
	size_t location,
	int32_t code,
	const char* layerPrefix,
	const char* msg,
	void* userData)
{
	static_cast<void>(flags);
	static_cast<void>(objType);
	static_cast<void>(obj);
	static_cast<void>(location);
	static_cast<void>(code);
	static_cast<void>(layerPrefix);
	static_cast<void>(userData);

	// #todo-vulkan: Switch to CYLOG()
	std::cerr << "[Vulkan validation layer] " << msg << std::endl;
	return VK_FALSE;
}

VkResult CreateDebugReportCallbackEXT(
	VkInstance instance,
	const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkDebugReportCallbackEXT* pCallback)
{
	auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
	if (func != nullptr)
	{
		return func(instance, pCreateInfo, pAllocator, pCallback);
	}
	else
	{
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

//////////////////////////////////////////////////////////////////////////

VulkanDevice::~VulkanDevice()
{
	// #todo-vulkan
}

void VulkanDevice::initialize(const RenderDeviceCreateParams& createParams)
{
	CYLOG(LogVulkan, Log, TEXT("=== Initialize Vulkan ==="));

	// Initialization steps
	// 1. VkInstance
	// 2. Vulkan debug callback
	// 3. KHR surface
	// 4. VkPhysicalDevice
	// 5. VkDevice
	// 6. Debug marker
	// 7. VkCommandPool
	// 8. Swapchain wrapper
	// 9. VkCommandBuffer
	// 10. VkSemaphore

	CYLOG(LogVulkan, Log, TEXT("> Create a VkInstance"));
	{
		if (createParams.enableDebugLayer)
		{
			enableDebugLayer = checkValidationLayerSupport();
			CHECK(enableDebugLayer);
		}

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = VK_APPINFO_APPNAME;
		appInfo.applicationVersion = VK_APPINFO_APPVER;
		appInfo.pEngineName = VK_APPINFO_ENGINENAME;
		appInfo.engineVersion = VK_APPINFO_ENGINEVER;
		appInfo.apiVersion = VK_MAX_API;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		std::vector<const char*> extensions;
		getRequiredExtensions(extensions);
		createInfo.enabledExtensionCount = static_cast<uint32>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();
		if (enableDebugLayer)
		{
			createInfo.enabledLayerCount = static_cast<uint32>(REQUIRED_VALIDATION_LAYERS.size());
			createInfo.ppEnabledLayerNames = REQUIRED_VALIDATION_LAYERS.data();
		}

		VkResult result = vkCreateInstance(&createInfo, nullptr, &vkInstance);
		CHECK(result == VK_SUCCESS);
	}

	CYLOG(LogVulkan, Log, TEXT("> Setup Vulkan debug callback"));
	{
		if (enableDebugLayer)
		{
			VkDebugReportCallbackCreateInfoEXT createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
			createInfo.pfnCallback = GVulkanDebugCallback;

			VkResult ret = CreateDebugReportCallbackEXT(vkInstance, &createInfo, nullptr, &vkDebugCallback);
			CHECK(ret == VK_SUCCESS);
		}
	}

#if PLATFORM_WINDOWS
	CYLOG(LogVulkan, Log, TEXT("> Create KHR surface"));
	vkSurface = ::createVkSurfaceKHR_win32(vkInstance, createParams.nativeWindowHandle);
#else
	#error Not implemented yet
#endif

	CYLOG(LogVulkan, Log, TEXT("> Pick a physical device"));
	{
		uint32 deviceCount = 0;
		vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);
		CHECK(deviceCount != 0);

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(vkInstance, &deviceCount, devices.data());
		for (const VkPhysicalDevice& physDevice : devices)
		{
			if (isDeviceSuitable(physDevice))
			{
				vkPhysicalDevice = physDevice;
				break;
			}
		}
		CHECK(vkPhysicalDevice != VK_NULL_HANDLE);
	}

	// #todo-vulkan: Check debug marker support

	CYLOG(LogVulkan, Log, TEXT("> Create a logical device"));
	{
		QueueFamilyIndices indices = findQueueFamilies(vkPhysicalDevice, vkSurface);

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

		float queuePriority = 1.0f; // in range 0.0 ~ 1.0
		for (int queueFamily : uniqueQueueFamilies)
		{
			// Describes # of queues we want for a single queue family
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceFeatures deviceFeatures{};
		deviceFeatures.samplerAnisotropy = VK_TRUE;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.queueCreateInfoCount = static_cast<uint32>(queueCreateInfos.size());
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = static_cast<uint32>(REQUIRED_DEVICE_EXTENSIONS.size());
		createInfo.ppEnabledExtensionNames = REQUIRED_DEVICE_EXTENSIONS.data();
		if (enableDebugLayer)
		{
			createInfo.enabledLayerCount = static_cast<uint32>(REQUIRED_VALIDATION_LAYERS.size());
			createInfo.ppEnabledLayerNames = REQUIRED_VALIDATION_LAYERS.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		const VkAllocationCallbacks* allocator = nullptr;
		VkResult ret = vkCreateDevice(vkPhysicalDevice, &createInfo, allocator, &vkDevice);
		CHECK(ret == VK_SUCCESS);

		vkGetDeviceQueue(vkDevice, indices.graphicsFamily, 0, &vkGraphicsQueue);
		vkGetDeviceQueue(vkDevice, indices.presentFamily, 0, &vkPresentQueue);
	}

	// Determine swapchain image count first.
	swapChain = new VulkanSwapchain;
	static_cast<VulkanSwapchain*>(swapChain)->preinitialize(this);

	{
		commandQueue = new VulkanRenderCommandQueue;
		commandQueue->initialize(this);

		for (uint32 ix = 0; ix < swapChain->getBufferCount(); ++ix)
		{
			RenderCommandAllocator* allocator = new VulkanRenderCommandAllocator;
			allocator->initialize(this);
			commandAllocators.push_back(allocator);
		}

		commandList = new VulkanRenderCommandList;
		commandList->initialize(this);
	}

	swapChain->initialize(
		this,
		createParams.nativeWindowHandle,
		createParams.windowWidth,
		createParams.windowHeight);

	CYLOG(LogVulkan, Log, TEXT("> Create semaphores for rendering"));
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkResult ret;

		ret = vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &vkImageAvailableSemaphore);
		CHECK(ret == VK_SUCCESS);

		ret = vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &vkRenderFinishedSemaphore);
		CHECK(ret == VK_SUCCESS);
	}
}

void VulkanDevice::recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height)
{
	HWND hwnd = (HWND)nativeWindowHandle;
	// #todo-vulkan
	//CHECK_NO_ENTRY();
}

void VulkanDevice::flushCommandQueue()
{
	VkResult ret = vkQueueWaitIdle(vkGraphicsQueue);
	CHECK(ret == VK_SUCCESS);
}

bool VulkanDevice::supportsRayTracing()
{
	// #todo-vulkan: vk_nv_ray_tracing
	CHECK_NO_ENTRY();
	return false;
}

VertexBuffer* VulkanDevice::createVertexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName)
{
	VulkanVertexBuffer* buffer = new VulkanVertexBuffer;
	buffer->initialize(sizeInBytes);
	return buffer;
}

VertexBuffer* VulkanDevice::createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes)
{
	VulkanVertexBuffer* buffer = new VulkanVertexBuffer;
	buffer->initializeWithinPool(pool, offsetInPool, sizeInBytes);
	return buffer;
}

IndexBuffer* VulkanDevice::createIndexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName)
{
	VulkanIndexBuffer* buffer = new VulkanIndexBuffer;
	buffer->initialize(sizeInBytes);
	return buffer;
}

IndexBuffer* VulkanDevice::createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes)
{
	VulkanIndexBuffer* buffer = new VulkanIndexBuffer;
	//buffer->initialize(data, sizeInBytes, format);
	return buffer;
}

Texture* VulkanDevice::createTexture(const TextureCreateParams& createParams)
{
	VulkanTexture* texture = new VulkanTexture;
	texture->initialize(createParams);
	return texture;
}

ShaderStage* VulkanDevice::createShader(EShaderStage shaderStage, const char* debugName)
{
	return new VulkanShaderStage(shaderStage, debugName);
}

RootSignature* VulkanDevice::createRootSignature(const RootSignatureDesc& desc)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
	return nullptr;
}

PipelineState* VulkanDevice::createGraphicsPipelineState(const GraphicsPipelineDesc& desc)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
	return nullptr;
}

DescriptorHeap* VulkanDevice::createDescriptorHeap(const DescriptorHeapDesc& desc)
{
	// #todo-vulkan
	//CHECK_NO_ENTRY();
	return nullptr;
}

ConstantBuffer* VulkanDevice::createConstantBuffer(DescriptorHeap* descriptorHeap, uint32 heapSize, uint32 payloadSize)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
	return nullptr;
}

void VulkanDevice::copyDescriptors(
	uint32 numDescriptors,
	DescriptorHeap* destHeap,
	uint32 destHeapDescriptorStartOffset,
	DescriptorHeap* srcHeap,
	uint32 srcHeapDescriptorStartOffset)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanDevice::setObjectDebugName(
	VkDebugReportObjectTypeEXT objectType,
	uint64 objectHandle,
	const char* debugName)
{
	// #todo-vulkan
	//if (canEnableDebugMarker)
	//{
	//	VkDebugMarkerObjectNameInfoEXT info{};
	//	info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
	//	info.objectType = objectType;
	//	info.object = objectHandle;
	//	info.pObjectName = debugName;
	//	vkDebugMarkerSetObjectName(vkDevice, &info);
	//}
}

VkCommandPool VulkanDevice::getTempCommandPool() const
{
	return static_cast<VulkanRenderCommandAllocator*>(commandAllocators[0])->getRawCommandPool();
}

bool VulkanDevice::checkValidationLayerSupport()
{
	uint32 layerCount;
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

#if PLATFORM_WINDOWS
	extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	extensions.push_back("VK_KHR_win32_surface");
#else
	#error Not implemented yet
#endif

	if (enableDebugLayer)
	{
		extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	}
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice physDevice)
{
	QueueFamilyIndices indices = findQueueFamilies(physDevice, vkSurface);

	VkPhysicalDeviceProperties deviceProperties;
	vkGetPhysicalDeviceProperties(physDevice, &deviceProperties);
	VkPhysicalDeviceFeatures deviceFeatures;
	vkGetPhysicalDeviceFeatures(physDevice, &deviceFeatures);

	bool extensionsSupported = checkDeviceExtensionSupport(physDevice);
	bool swapChainAdequate = false;
	if (extensionsSupported)
	{
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physDevice);
		swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
	}

	return indices.isComplete() && extensionsSupported
		&& swapChainAdequate && deviceFeatures.samplerAnisotropy;
}

bool VulkanDevice::checkDeviceExtensionSupport(VkPhysicalDevice physDevice)
{
	uint32 extensionCount;
	vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount, nullptr);
	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount, availableExtensions.data());
	std::set<std::string> requiredExtensions(REQUIRED_DEVICE_EXTENSIONS.begin(), REQUIRED_DEVICE_EXTENSIONS.end());
	for (const auto& extension : availableExtensions)
	{
		requiredExtensions.erase(extension.extensionName);
	}
	return requiredExtensions.empty();
}

SwapChainSupportDetails VulkanDevice::querySwapChainSupport(VkPhysicalDevice physDevice)
{
	SwapChainSupportDetails details;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, vkSurface, &details.capabilities);
	
	uint32 formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, vkSurface, &formatCount, nullptr);
	if (formatCount != 0)
	{
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, vkSurface, &formatCount, details.formats.data());
	}

	uint32 presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, vkSurface, &presentModeCount, nullptr);
	if (presentModeCount != 0)
	{
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, vkSurface, &presentModeCount, details.presentModes.data());
	}

	return details;
}

VkSurfaceFormatKHR VulkanDevice::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
	if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
	{
		return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	}
	for (const auto& availableFormat : availableFormats)
	{
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM
			&& availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			return availableFormat;
		}
	}
	return availableFormats[0];
}

VkPresentModeKHR VulkanDevice::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
	for (const auto& availablePresentMode : availablePresentModes)
	{
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			return availablePresentMode;
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanDevice::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32 windowWidth, uint32 windowHeight)
{
	if (capabilities.currentExtent.width != (std::numeric_limits<uint32>::max)())
	{
		return capabilities.currentExtent;
	}
	else
	{
		const auto& minExtent = capabilities.minImageExtent;
		const auto& maxExtent = capabilities.maxImageExtent;
		VkExtent2D actualExtent{ windowWidth, windowHeight };
		actualExtent.width = (std::max)(minExtent.width, (std::min)(maxExtent.width, actualExtent.width));
		actualExtent.height = (std::max)(minExtent.height, (std::min)(maxExtent.height, actualExtent.height));
		return actualExtent;
	}
}

#endif // COMPILE_BACKEND_VULKAN
