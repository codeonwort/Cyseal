#include "vk_device.h"
#include "vk_shader.h"
#include "vk_utils.h"
#include "core/assertion.h"
#include <algorithm>
#include <limits>
#include <string>
#include <array>
#include <set>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

// #todo-crossapi: Dynamic loading
#pragma comment(lib, "vulkan-1.lib")

DEFINE_LOG_CATEGORY(LogVulkan);

const std::vector<const char*> REQUIRED_VALIDATION_LAYERS = {
	"VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> REQUIRED_DEVICE_EXTENSIONS = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#include <iostream>
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
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
	CYLOG(LogVulkan, Log, TEXT("=== Initialize Vulkan ==="));

	// [Finished]
	//createVkInstance();
	//setupDebugCallback();
	//createSurface();
	//pickPhysicalDevice();
	//createLogicalDevice();
	//createSwapChain();
	//createImageViews();
	//createRenderPass();

	// #todo-vulkan
	//createCommandPool();
	//createDepthResources();
	//createFramebuffers();
	//create3DModels();
	//createCommandBuffers();
	//createSemaphores();

	CYLOG(LogVulkan, Log, TEXT("> Create a VkInstance"));
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
		createInfo.enabledExtensionCount = static_cast<uint32>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();
		if (enableDebugLayer)
		{
			createInfo.enabledLayerCount = static_cast<uint32>(REQUIRED_VALIDATION_LAYERS.size());
			createInfo.ppEnabledLayerNames = REQUIRED_VALIDATION_LAYERS.data();
		}

		VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
		CHECK(result == VK_SUCCESS);
	}

	CYLOG(LogVulkan, Log, TEXT("> Setup Vulkan debug callback"));
	{
		if (enableDebugLayer)
		{
			VkDebugReportCallbackCreateInfoEXT createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
			createInfo.pfnCallback = vulkanDebugCallback;

			if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS) {
				CHECK_NO_ENTRY();
			}
		}
	}

	CYLOG(LogVulkan, Log, TEXT("> Create KHR surface"));
	{
		VkResult err;
		VkWin32SurfaceCreateInfoKHR sci;
		PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;

		vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
		if (!vkCreateWin32SurfaceKHR)
		{
			CYLOG(LogVulkan, Fatal, TEXT("Win32: Vulkan instance missing VK_KHR_win32_surface extension"));
		}

		memset(&sci, 0, sizeof(sci));
		sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		sci.hinstance = GetModuleHandle(NULL);
		sci.hwnd = createParams.hwnd;

		const VkAllocationCallbacks* allocator = nullptr;
		err = vkCreateWin32SurfaceKHR(instance, &sci, allocator, &surface);
		if (err)
		{
			CYLOG(LogVulkan, Fatal, TEXT("Failed to create Vulkan surface"));
		}
	}

	CYLOG(LogVulkan, Log, TEXT("> Pick a physical device"));
	{
		uint32 deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

		CHECK(deviceCount != 0);

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
		for (const auto& physDevice : devices)
		{
			if (isDeviceSuitable(physDevice))
			{
				physicalDevice = physDevice;
				break;
			}
		}

		CHECK(physicalDevice != VK_NULL_HANDLE);
	}

	CYLOG(LogVulkan, Log, TEXT("> Create a logical device"));
	{
		QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

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
		VkResult ret = vkCreateDevice(physicalDevice, &createInfo, allocator, &device);
		CHECK(ret == VK_SUCCESS);

		vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
		vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
	}

	CYLOG(LogVulkan, Log, TEXT("Create swapchain"));
	{
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
		VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
		VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
		VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, createParams.windowWidth, createParams.windowHeight);
		uint32 imageCount = swapChainSupport.capabilities.minImageCount + 1;
		// maxImageCount = 0 means there's no limit besides memory requirements
		if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
		{
			imageCount = swapChainSupport.capabilities.maxImageCount;
		}
		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		// 1 unless developming a stereoscopic 3D application
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
		uint32 queueFamilyIndices[] = { static_cast<uint32>(indices.graphicsFamily), static_cast<uint32>(indices.presentFamily) };
		if (indices.graphicsFamily != indices.presentFamily)
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else
		{
			// best performance. an image is owned by one queue family at a time
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0;
			createInfo.pQueueFamilyIndices = nullptr;
		}
		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		VkResult ret = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
		CHECK(ret == VK_SUCCESS);

		vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
		swapchainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
		swapchainImageFormat = surfaceFormat.format;
		swapchainExtent = extent;
	}

	CYLOG(LogVulkan, Log, TEXT("> Create image views for swapchain images"));
	{
		swapchainImageViews.resize(swapchainImages.size());
		for (size_t i = 0; i < swapchainImages.size(); ++i)
		{
			swapchainImageViews[i] = createImageView(
				device,
				swapchainImages[i],
				swapchainImageFormat,
				VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	CYLOG(LogVulkan, Log, TEXT("> Create render pass for back-buffer"));
	{
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = swapchainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription depthAttachment = {};
		depthAttachment.format = findDepthFormat(physicalDevice);
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentRef = {};
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pDepthStencilAttachment = &depthAttachmentRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = {
			colorAttachment, depthAttachment
		};
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		VkResult ret = vkCreateRenderPass(device, &renderPassInfo, nullptr, &backbufferRenderPass);
		CHECK(ret == VK_SUCCESS);
	}

	CYLOG(LogVulkan, Log, TEXT("> Create command pool"));
	{
		QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
		poolInfo.flags = 0;

		VkResult ret = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
		CHECK(ret == VK_SUCCESS);
	}

	CYLOG(LogVulkan, Log, TEXT("> Create depth resources for backbuffer"));
	{
		VkFormat depthFormat = findDepthFormat(physicalDevice);

		createImage(physicalDevice, device, swapchainExtent.width, swapchainExtent.height,
			depthFormat,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			depthImage, depthImageMemory);

		depthImageView = createImageView(device, depthImage, depthFormat,
			VK_IMAGE_ASPECT_DEPTH_BIT);

		transitionImageLayout(
			device, commandPool, graphicsQueue,
			depthImage, depthFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	CYLOG(LogVulkan, Log, TEXT("> Create framebuffers for backbuffer"));
	{
		swapchainFramebuffers.resize(swapchainImageViews.size());

		for (size_t i = 0; i < swapchainImageViews.size(); ++i)
		{
			std::array<VkImageView, 2> attachments = { swapchainImageViews[i], depthImageView };
			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = backbufferRenderPass;
			framebufferInfo.attachmentCount = static_cast<uint32>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = swapchainExtent.width;
			framebufferInfo.height = swapchainExtent.height;
			framebufferInfo.layers = 1;

			VkResult ret = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]);
			CHECK(ret == VK_SUCCESS);
		}
	}

	CYLOG(LogVulkan, Log, TEXT("> Create command buffers"));
	{
		commandBuffers.resize(swapchainFramebuffers.size());

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = static_cast<uint32>(commandBuffers.size());
		
		VkResult ret = vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
		CHECK(ret == VK_SUCCESS);

		for (size_t i = 0; i < commandBuffers.size(); ++i)
		{
			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
			beginInfo.pInheritanceInfo = nullptr;
			vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

			std::array<VkClearValue, 2> clearValues;
			clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = backbufferRenderPass;
			renderPassInfo.framebuffer = swapchainFramebuffers[i];
			renderPassInfo.renderArea.offset = { 0,0 };
			renderPassInfo.renderArea.extent = swapchainExtent;
			renderPassInfo.clearValueCount = static_cast<uint32>(clearValues.size());
			renderPassInfo.pClearValues = clearValues.data();
			
			vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			{
				// #todo-vulkan: Drawing commands here
				//triangle.commitCommands(commandBuffers[i]);
			}
			vkCmdEndRenderPass(commandBuffers[i]);

			VkResult ret = vkEndCommandBuffer(commandBuffers[i]);
			CHECK(ret == VK_SUCCESS);
		}
	}

	CYLOG(LogVulkan, Log, TEXT("> Create semaphores for rendering"));
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkResult ret;

		ret = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore);
		CHECK(ret == VK_SUCCESS);

		ret = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore);
		CHECK(ret == VK_SUCCESS);
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

Texture* VulkanDevice::createTexture(const TextureCreateParams& createParams)
{
	// #todo-vulkan
	return nullptr;
}

Shader* VulkanDevice::createShader()
{
	return new VulkanShader;
}

RootSignature* VulkanDevice::createRootSignature(const RootSignatureDesc& desc)
{
	// #todo-vulkan
	return nullptr;
}

PipelineState* VulkanDevice::createGraphicsPipelineState(const GraphicsPipelineDesc& desc)
{
	// #todo-vulkan
	return nullptr;
}

DescriptorHeap* VulkanDevice::createDescriptorHeap(const DescriptorHeapDesc& desc)
{
	// #todo-vulkan
	return nullptr;
}

ConstantBuffer* VulkanDevice::createConstantBuffer(DescriptorHeap* descriptorHeap, uint32 heapSize, uint32 payloadSize)
{
	// #todo-vulkan
	return nullptr;
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

	// #todo-crossplatform: Windows only for now
	{
		extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
		extensions.push_back("VK_KHR_win32_surface");
	}

	if (enableDebugLayer)
	{
		extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	}
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice physDevice)
{
	QueueFamilyIndices indices = findQueueFamilies(physDevice);

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

VulkanDevice::QueueFamilyIndices VulkanDevice::findQueueFamilies(VkPhysicalDevice physDevice)
{
	QueueFamilyIndices indices;
	uint32 queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, queueFamilies.data());
	int i = 0;
	for (const auto& queueFamily : queueFamilies)
	{
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			indices.graphicsFamily = i;
		}
		VkBool32 presentSupport = false;

		CYLOG(LogVulkan, Log, TEXT("Check surface present support"));

		vkGetPhysicalDeviceSurfaceSupportKHR(physDevice, i, surface, &presentSupport);
		if (queueFamily.queueCount > 0 && presentSupport)
		{
			indices.presentFamily = i;
		}
		if (indices.isComplete())
		{
			break;
		}
		++i;
	}
	return indices;
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

VulkanDevice::SwapChainSupportDetails VulkanDevice::querySwapChainSupport(VkPhysicalDevice physDevice)
{
	SwapChainSupportDetails details;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, surface, &details.capabilities);
	
	uint32 formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, nullptr);
	if (formatCount != 0)
	{
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, details.formats.data());
	}

	uint32 presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &presentModeCount, nullptr);
	if (presentModeCount != 0)
	{
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &presentModeCount, details.presentModes.data());
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
