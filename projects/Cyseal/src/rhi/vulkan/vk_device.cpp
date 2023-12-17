#include "vk_device.h"
#include "core/platform.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_render_command.h"
#include "vk_swapchain.h"
#include "vk_texture.h"
#include "vk_buffer.h"
#include "vk_shader.h"
#include "vk_pipeline_state.h"
#include "vk_descriptor.h"
#include "vk_utils.h"
#include "vk_into.h"
#include "core/platform.h"
#include "core/assertion.h"
#include "rhi/swap_chain.h"

#include <vulkan/vulkan.h>
#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <limits>
#include <string>
#include <array>
#include <set>
#include <iostream>

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

	CYLOG(LogVulkan, Warning, TEXT("[validation layer] %S"), msg);

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/PFN_vkDebugReportCallbackEXT.html
	// The application should always return VK_FALSE. The VK_TRUE value is reserved for use in layer development.
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

// https://www.saschawillems.de/blog/2016/05/28/tutorial-on-using-vulkans-vk_ext_debug_marker-with-renderdoc/
static bool checkVkDebugMarkerSupport(VkPhysicalDevice physDevice)
{
	uint32 extensionCount;
	vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount, nullptr);
	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount, availableExtensions.data());

	for (const auto& extension : availableExtensions)
	{
		if (0 == strcmp(extension.extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
		{
			return true;
		}
	}

	return false;
}

VkDevice getVkDevice()
{
	return static_cast<VulkanDevice*>(gRenderDevice)->getRaw();
}

//////////////////////////////////////////////////////////////////////////

VulkanDevice::~VulkanDevice()
{
	// #todo-vulkan
}

void VulkanDevice::onInitialize(const RenderDeviceCreateParams& createParams)
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

		VkApplicationInfo appInfo{
			.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pNext              = nullptr,
			.pApplicationName   = VK_APPINFO_APPNAME,
			.applicationVersion = VK_APPINFO_APPVER,
			.pEngineName        = VK_APPINFO_ENGINENAME,
			.engineVersion      = VK_APPINFO_ENGINEVER,
			.apiVersion         = VK_MAX_API,
		};

		uint32 enabledLayerCount = 0;
		const char* const* ppEnabledLayerNames = nullptr;
		if (enableDebugLayer)
		{
			enabledLayerCount = static_cast<uint32>(REQUIRED_VALIDATION_LAYERS.size());
			ppEnabledLayerNames = REQUIRED_VALIDATION_LAYERS.data();
		}

		std::vector<const char*> extensions;
		getRequiredExtensions(extensions);

		VkInstanceCreateInfo createInfo{
			.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pNext                   = nullptr,
			.flags                   = 0,
			.pApplicationInfo        = &appInfo,
			.enabledLayerCount       = enabledLayerCount,
			.ppEnabledLayerNames     = ppEnabledLayerNames,
			.enabledExtensionCount   = static_cast<uint32>(extensions.size()),
			.ppEnabledExtensionNames = extensions.data(),
		};

		VkResult result = vkCreateInstance(&createInfo, nullptr, &vkInstance);
		CHECK(result == VK_SUCCESS);
	}

	CYLOG(LogVulkan, Log, TEXT("> Setup Vulkan debug callback"));
	{
		if (enableDebugLayer)
		{
			VkDebugReportCallbackCreateInfoEXT createInfo{
				.sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
				.pNext       = nullptr,
				.flags       = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
				.pfnCallback = GVulkanDebugCallback,
				.pUserData   = nullptr,
			};

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
		
		// This is true if the process was launched via a frame debugger (e.g., RenderDoc).
		canEnableDebugMarker = checkVkDebugMarkerSupport(vkPhysicalDevice);
	}

	CYLOG(LogVulkan, Log, TEXT("> Create a logical device"));
	{
		QueueFamilyIndices indices = findQueueFamilies(vkPhysicalDevice, vkSurface);

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

		float queuePriority = 1.0f; // in range 0.0 ~ 1.0
		for (int queueFamily : uniqueQueueFamilies)
		{
			// Describes # of queues we want for a single queue family
			VkDeviceQueueCreateInfo queueCreateInfo{
				.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.pNext            = nullptr,
				.flags            = (VkDeviceQueueCreateFlagBits)0,
				.queueFamilyIndex = (uint32)queueFamily,
				.queueCount       = 1,
				.pQueuePriorities = &queuePriority,
			};
			queueCreateInfos.push_back(queueCreateInfo);
		}

		// #note-vulkan: Too many fields to use designated initializer.
		VkPhysicalDeviceFeatures deviceFeatures{};
		//deviceFeatures.imageCubeArray = VK_TRUE;
		deviceFeatures.samplerAnisotropy = VK_TRUE;
		//deviceFeatures.multiDrawIndirect = VK_TRUE;

		std::vector<const char*> enabledExtensions(
			REQUIRED_DEVICE_EXTENSIONS.begin(),
			REQUIRED_DEVICE_EXTENSIONS.end());
		if (canEnableDebugMarker)
		{
			enabledExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
		}

		uint32 enabledLayerCount = 0;
		const char* const* ppEnabledLayerNames = nullptr;
		if (enableDebugLayer)
		{
			enabledLayerCount = static_cast<uint32>(REQUIRED_VALIDATION_LAYERS.size());
			ppEnabledLayerNames = REQUIRED_VALIDATION_LAYERS.data();
		}

		VkDeviceCreateInfo createInfo{
			.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext                   = nullptr,
			.flags                   = 0, // VkDeviceCreateFlags
			.queueCreateInfoCount    = static_cast<uint32>(queueCreateInfos.size()),
			.pQueueCreateInfos       = queueCreateInfos.data(),
			.enabledLayerCount       = enabledLayerCount,
			.ppEnabledLayerNames     = ppEnabledLayerNames,
			.enabledExtensionCount   = (uint32)enabledExtensions.size(),
			.ppEnabledExtensionNames = enabledExtensions.data(),
			.pEnabledFeatures        = &deviceFeatures,
		};

		const VkAllocationCallbacks* allocator = nullptr;
		VkResult ret = vkCreateDevice(vkPhysicalDevice, &createInfo, allocator, &vkDevice);
		CHECK(ret == VK_SUCCESS);

		vkGetDeviceQueue(vkDevice, indices.graphicsFamily, 0, &vkGraphicsQueue);
		vkGetDeviceQueue(vkDevice, indices.presentFamily, 0, &vkPresentQueue);
	}

	// Get debug marker functions.
	{
		if (canEnableDebugMarker)
		{
			vkCmdDebugMarkerBegin = (PFN_vkCmdDebugMarkerBeginEXT)vkGetDeviceProcAddr(vkDevice, "vkCmdDebugMarkerBeginEXT");
			vkCmdDebugMarkerEnd = (PFN_vkCmdDebugMarkerEndEXT)vkGetDeviceProcAddr(vkDevice, "vkCmdDebugMarkerEndEXT");
			vkDebugMarkerSetObjectName = (PFN_vkDebugMarkerSetObjectNameEXT)vkGetDeviceProcAddr(vkDevice, "vkDebugMarkerSetObjectNameEXT");

			if (vkCmdDebugMarkerBegin != VK_NULL_HANDLE
				&& vkCmdDebugMarkerEnd != VK_NULL_HANDLE
				&& vkDebugMarkerSetObjectName != VK_NULL_HANDLE)
			{
				CYLOG(LogVulkan, Log, L"Enable extension: debug marker");
			}
			else
			{
				canEnableDebugMarker = false;
				CYLOG(LogVulkan, Log, L"Can't enable extension: debug marker procedures not found");
			}
		}
		else
		{
			CYLOG(LogVulkan, Log, L"Can't enable extension: debug marker not found");
		}
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

			RenderCommandList* commandList = new VulkanRenderCommandList;
			commandList->initialize(this);
			commandLists.push_back(commandList);
		}
	}

	swapChain->initialize(
		this,
		createParams.nativeWindowHandle,
		createParams.windowWidth,
		createParams.windowHeight);

	CYLOG(LogVulkan, Log, TEXT("> Create semaphores for rendering"));
	{
		VkSemaphoreCreateInfo semaphoreInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0, // VkSemaphoreCreateFlags
		};

		VkResult ret;

		ret = vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &vkSwapchainImageAvailableSemaphore);
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

void VulkanDevice::initializeDearImgui()
{
	// #wip-dearimgui: initializeDearImgui()
	// https://vkguide.dev/docs/extra-chapter/implementing_imgui/
	// https://frguthmann.github.io/posts/vulkan_imgui/
#if 0
	RenderDevice::initializeDearImgui();

	QueueFamilyIndices queueFamily = findQueueFamilies(vkPhysicalDevice, vkSurface);

	ImGui_ImplVulkan_InitInfo initInfo{
		.Instance        = vkInstance,
		.PhysicalDevice  = vkPhysicalDevice,
		.Device          = vkDevice,
		.QueueFamily     = (uint32_t)queueFamily.graphicsFamily, // wip
		.Queue           = vkGraphicsQueue,
		.PipelineCache   = VK_NULL_HANDLE, // wip
		.DescriptorPool  = VK_NULL_HANDLE, // wip
		.Subpass         = 0, // wip
		.MinImageCount   = swapChain->getBufferCount(), // wip
		.ImageCount      = swapChain->getBufferCount(), // wip
		.MSAASamples     = VK_SAMPLE_COUNT_4_BIT, // wip
		.Allocator       = nullptr, // wip
		.CheckVkResultFn = nullptr, // wip
	};
	
	VkRenderPass vkRenderPass = VK_NULL_HANDLE;

	ImGui_ImplVulkan_Init(&initInfo, vkRenderPass);
#endif
}

void VulkanDevice::beginDearImguiNewFrame()
{
	// #wip-dearimgui: beginDearImguiNewFrame()
#if 0
	ImGui_ImplVulkan_NewFrame();
#endif
}

void VulkanDevice::renderDearImgui(RenderCommandList* commandList)
{
	// #wip-dearimgui: renderDearImgui()
#if 0
	VkCommandBuffer vkCommandBuffer = static_cast<VulkanRenderCommandList*>(commandList)->currentCommandBuffer;
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCommandBuffer, vkPipeline);
#endif
}

void VulkanDevice::shutdownDearImgui()
{
	// #wip-dearimgui: shutdownDearImgui()
#if 0
	RenderDevice::shutdownDearImgui();

	ImGui_ImplVulkan_Shutdown();
#endif
}

VertexBuffer* VulkanDevice::createVertexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName)
{
	VulkanVertexBuffer* buffer = new VulkanVertexBuffer;
	buffer->initialize(sizeInBytes);
	if (inDebugName != nullptr)
	{
		std::string debugNameA;
		wstr_to_str(inDebugName, debugNameA);
		setObjectDebugName(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, (uint64)buffer->getVkBuffer(), debugNameA.c_str());
	}
	return buffer;
}

VertexBuffer* VulkanDevice::createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes)
{
	VulkanVertexBuffer* buffer = new VulkanVertexBuffer;
	buffer->initializeWithinPool(pool, offsetInPool, sizeInBytes);
	return buffer;
}

IndexBuffer* VulkanDevice::createIndexBuffer(uint32 sizeInBytes, EPixelFormat format, const wchar_t* inDebugName)
{
	VulkanIndexBuffer* buffer = new VulkanIndexBuffer;
	buffer->initialize(sizeInBytes, format);
	if (inDebugName != nullptr)
	{
		std::string debugNameA;
		wstr_to_str(inDebugName, debugNameA);
		setObjectDebugName(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, (uint64)buffer->getVkBuffer(), debugNameA.c_str());
	}
	return buffer;
}

IndexBuffer* VulkanDevice::createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes, EPixelFormat format)
{
	VulkanIndexBuffer* buffer = new VulkanIndexBuffer;
	buffer->initializeWithinPool(pool, offsetInPool, sizeInBytes);
	return buffer;
}

Buffer* VulkanDevice::createBuffer(const BufferCreateParams& createParams)
{
	// #todo-vulkan
	return nullptr;
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

RootSignature* VulkanDevice::createRootSignature(const RootSignatureDesc& inDesc)
{
	// #wip: Needs VkDescriptorSetLayout, and VkDescriptorSet first.
	VkPipelineLayoutCreateInfo desc{
		.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext                  = nullptr,
		.flags                  = (VkPipelineLayoutCreateFlagBits)0,
		.setLayoutCount         = 1,
		.pSetLayouts            = 0, // #wip
		.pushConstantRangeCount = 0,
		.pPushConstantRanges    = nullptr, // #wip: Push constant
	};

	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkResult ret = vkCreatePipelineLayout(vkDevice, &desc, nullptr, &vkPipelineLayout);
	CHECK(ret == VK_SUCCESS);

	return new VulkanPipelineLayout(vkPipelineLayout);
}

PipelineState* VulkanDevice::createGraphicsPipelineState(const GraphicsPipelineDesc& inDesc)
{
	// #wip: PSO conversion
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	VkRenderPass vkRenderPass = VK_NULL_HANDLE;

#if 0
	// VkRenderPass
	{
		std::vector<VkAttachmentDescription> colorAttachments;
		colorAttachments.reserve(inDesc.numRenderTargets);
		for (uint32 i = 0; i < inDesc.numRenderTargets; ++i)
		{
			VkAttachmentDescription attachmentDesc{
				.flags          = (VkAttachmentDescriptionFlagBits)0,
				.format         = into_vk::pixelFormat(inDesc.rtvFormats[i]),
				// #todo-vulkan: Vulkan allows different sample counts between color attachments?
				// DX12 requires same count for all attachments:
				// https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ns-dxgicommon-dxgi_sample_desc
				.samples        = into_vk::sampleCount(inDesc.sampleDesc.count),
				// #todo-vulkan: loadOp, storeOp for color attachment
				.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				// #todo-vulkan: initialLayout, finalLayout for color attachment
				.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			colorAttachments.emplace_back(attachmentDesc);
		}

		VkAttachmentDescription depthAttachment{
			.flags          = (VkAttachmentDescriptionFlagBits)0,
			.format         = into_vk::pixelFormat(inDesc.dsvFormat),
			.samples        = VK_SAMPLE_COUNT_1_BIT,
			.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			// #todo-vulkan: initialLayout, finalLayout for depth attachment
			.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		std::vector<VkAttachmentReference> colorAttachmentRef(inDesc.numRenderTargets);
		for (uint32 i = 0; i < inDesc.numRenderTargets; ++i)
		{
			colorAttachmentRef[i].attachment = i;
			colorAttachmentRef[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		VkAttachmentReference depthAttachmentRef{
			.attachment = inDesc.numRenderTargets, // Binding point right after color attachments
			.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass{
			.flags                   = (VkSubpassDescriptionFlagBits)0,
			.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount    = 0,
			.pInputAttachments       = nullptr,
			.colorAttachmentCount    = inDesc.numRenderTargets,
			.pColorAttachments       = colorAttachmentRef.data(),
			.pResolveAttachments     = nullptr,
			.pDepthStencilAttachment = inDesc.depthstencilDesc.depthEnable ? &depthAttachmentRef : nullptr,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments    = nullptr,
		};

		// #todo-vulkan: Subpass dependency
		// https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples
		//VkSubpassDependency subpassDependency{};

		std::vector<VkAttachmentDescription> attachmentDesc;
		for (uint32 i = 0; i < inDesc.numRenderTargets; ++i)
		{
			attachmentDesc.push_back(colorAttachments[i]);
		}
		if (inDesc.depthstencilDesc.depthEnable)
		{
			attachmentDesc.push_back(depthAttachment);
		}

		VkRenderPassCreateInfo renderPassDesc{
			.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.pNext           = nullptr,
			.flags           = (VkRenderPassCreateFlagBits)0,
			.attachmentCount = (uint32)attachmentDesc.size(),
			.pAttachments    = attachmentDesc.data(),
			.subpassCount    = 1,
			.pSubpasses      = &subpass,
			.dependencyCount = 0,
			.pDependencies   = nullptr,
		};

		VkResult ret = vkCreateRenderPass(vkDevice, &renderPassDesc, nullptr, &vkRenderPass);
		CHECK(ret == VK_SUCCESS);
	}

	// pStages
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	ShaderStage* shaderWrappers[] = { inDesc.vs, inDesc.hs, inDesc.ds, inDesc.gs, inDesc.ps };
	for (uint32 i = 0; i < _countof(shaderWrappers); ++i)
	{
		VulkanShaderStage* shaderWrapper = static_cast<VulkanShaderStage*>(shaderWrappers[i]);
		if (shaderWrapper == nullptr)
		{
			continue;
		}

		// #todo-vulkan: What is VkSpecializationInfo
		const VkSpecializationInfo* pSpecializationInfo = nullptr;

		VkPipelineShaderStageCreateInfo stageInfo{
			.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext               = nullptr,
			.flags               = (VkPipelineShaderStageCreateFlagBits)0,
			.stage               = shaderWrapper->getVkShaderStage(),
			.module              = shaderWrapper->getVkShaderModule(),
			.pName               = shaderWrapper->getEntryPointA(),
			.pSpecializationInfo = pSpecializationInfo,
		};
		shaderStages.emplace_back(stageInfo);
	}

	// pVertexInputState
	const uint32 numInputElements = (uint32)inDesc.inputLayout.elements.size();
	std::vector<VkVertexInputBindingDescription> inputBindings;
	std::vector<VkVertexInputAttributeDescription> inputAttributes(numInputElements);
	into_vk::vertexInputBindings(inDesc.inputLayout.elements, inputBindings);
	for (uint32 i = 0; i < numInputElements; ++i)
	{
		inputAttributes[i] = into_vk::vertexInputAttribute(inDesc.inputLayout.elements[i]);
	}
	VkPipelineVertexInputStateCreateInfo vertexInputDesc{
		.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pNext                           = nullptr,
		.flags                           = 0, // VkPipelineVertexInputStateCreateFlags
		.vertexBindingDescriptionCount   = (uint32)inputBindings.size(),
		.pVertexBindingDescriptions      = inputBindings.data(),
		.vertexAttributeDescriptionCount = (uint32)inputAttributes.size(),
		.pVertexAttributeDescriptions    = inputAttributes.data(),
	};

	// pInputAssemblyState
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{
		.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext                  = nullptr,
		.flags                  = 0, // VkPipelineInputAssemblyStateCreateFlags
		.topology               = into_vk::primitiveTopologyType(inDesc.primitiveTopologyType),
		.primitiveRestartEnable = VK_FALSE, // #todo-vulkan: Primitive Restart
	};

	// pRasterizationState
	VkPipelineRasterizationStateCreateInfo rasterizer{
		.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext                   = nullptr,
		.flags                   = 0, // VkPipelineRasterizationStateCreateFlags
		.depthClampEnable        = inDesc.rasterizerDesc.depthClipEnable,
		.rasterizerDiscardEnable = VK_FALSE, // #todo-vulkan: rasterizerDiscardEnable
		.polygonMode             = into_vk::polygonMode(inDesc.rasterizerDesc.fillMode),
		.cullMode                = into_vk::cullMode(inDesc.rasterizerDesc.cullMode),
		.frontFace               = inDesc.rasterizerDesc.frontCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable         = (0 != inDesc.rasterizerDesc.depthBias),
		.depthBiasConstantFactor = (float)inDesc.rasterizerDesc.depthBias,
		.depthBiasClamp          = inDesc.rasterizerDesc.depthBiasClamp,
		.depthBiasSlopeFactor    = inDesc.rasterizerDesc.slopeScaledDepthBias,
		.lineWidth               = 1.0f, // #todo-crossapi: vk-only
	};

	// pMultisampleState
	// #todo-vulkan: Support multisampling
	VkPipelineMultisampleStateCreateInfo multisampling{
		.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.pNext                 = nullptr,
		.flags                 = 0, // VkPipelineMultisampleStateCreateFlags
		.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable   = VK_FALSE,
		.minSampleShading      = 1.0f,
		.pSampleMask           = nullptr,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable      = VK_FALSE,
	};

	// pDepthStencilState
	VkPipelineDepthStencilStateCreateInfo depthStencil
		= into_vk::depthstencilDesc(inDesc.depthstencilDesc);

	// pColorBlendState
	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
	colorBlendAttachments.reserve(inDesc.numRenderTargets);
	for (uint32 i = 0; i < inDesc.numRenderTargets; ++i)
	{
		const RenderTargetBlendDesc& inBlend = inDesc.blendDesc.renderTarget[i];

		VkPipelineColorBlendAttachmentState attachmentState{
			.blendEnable         = inBlend.blendEnable,
			.srcColorBlendFactor = into_vk::blendFactor(inBlend.srcBlend),
			.dstColorBlendFactor = into_vk::blendFactor(inBlend.destBlend),
			.colorBlendOp        = into_vk::blendOp(inBlend.blendOp),
			.srcAlphaBlendFactor = into_vk::blendFactor(inBlend.srcBlendAlpha),
			.dstAlphaBlendFactor = into_vk::blendFactor(inBlend.destBlendAlpha),
			.alphaBlendOp        = into_vk::blendOp(inBlend.blendOpAlpha),
			.colorWriteMask      = into_vk::colorWriteMask(inBlend.renderTargetWriteMask),
		};
		colorBlendAttachments.emplace_back(attachmentState);
	}
	// #todo-vulkan: Independent blend state is an extension in Vulkan
	VkPipelineColorBlendStateCreateInfo colorBlendDesc{
		.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext           = nullptr,
		.flags           = 0, // VkPipelineColorBlendStateCreateFlags
		.logicOpEnable   = inDesc.blendDesc.renderTarget[0].logicOpEnable,
		.logicOp         = into_vk::logicOp(inDesc.blendDesc.renderTarget[0].logicOp),
		.attachmentCount = (uint32)colorBlendAttachments.size(),
		.pAttachments    = colorBlendAttachments.data(),
		// #todo-vulkan: Dynamic blend factor
		// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkCmdSetBlendConstants.html
		.blendConstants  = { 0.0f, 0.0f, 0.0f, 0.0f },
	};

	// pDynamicState
	// #todo-vulkan: DX12 always set them dynamically via RSSet~~ methods,
	// so we make them always dynamic in Vulkan backend also. Some render passes
	// would have fixed viewport and scissor, but we ignore the tiny overhead for them.
	std::array<VkDynamicState, 2> dynamicStates = {
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicStateInfo{
		.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pNext             = nullptr,
		.flags             = 0, // VkPipelineDynamicStateCreateFlags
		.dynamicStateCount = (uint32_t)dynamicStates.size(),
		.pDynamicStates    = dynamicStates.data(),
	};

	// layout
	// #wip: Take this as parameter
	RootSignature* inRootSignature = nullptr;
	VkPipelineLayout vkPipelineLayout = static_cast<VulkanPipelineLayout*>(inRootSignature)->getVkPipelineLayout();

	VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext               = nullptr,
		.flags               = 0, // VkPipelineCreateFlags
		.stageCount          = (uint32)shaderStages.size(),
		.pStages             = shaderStages.data(),
		.pVertexInputState   = &vertexInputDesc,
		.pInputAssemblyState = &inputAssembly,
		.pTessellationState  = nullptr, // #todo-vulkan: tessellation state
		.pViewportState      = nullptr, // #note-vulkan: viewport state is always dynamic in Cyseal
		.pRasterizationState = &rasterizer,
		.pMultisampleState   = &multisampling,
		.pDepthStencilState  = &depthStencil,
		.pColorBlendState    = &colorBlendDesc,
		.pDynamicState       = &dynamicStateInfo,
		.layout              = vkPipelineLayout,
		.renderPass          = vkRenderPass,
		.subpass             = 0, // index of the subpass in the render pass where this pipeline will be used.
		.basePipelineHandle  = VK_NULL_HANDLE,
		.basePipelineIndex   = -1,
	};

	VkResult ret = vkCreateGraphicsPipelines(
		vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline);
	CHECK(ret == VK_SUCCESS);

	return new VulkanGraphicsPipelineState(vkPipeline, vkRenderPass);
#endif
	return nullptr;
}

PipelineState* VulkanDevice::createComputePipelineState(const ComputePipelineDesc& desc)
{
	// #todo-vulkan
	return nullptr;
}

RaytracingPipelineStateObject* VulkanDevice::createRaytracingPipelineStateObject(
	const RaytracingPipelineStateObjectDesc& desc)
{
	// #todo-vulkan
	return nullptr;
}

RaytracingShaderTable* VulkanDevice::createRaytracingShaderTable(
	RaytracingPipelineStateObject* RTPSO,
	uint32 numShaderRecords,
	uint32 rootArgumentSize,
	const wchar_t* debugName)
{
	// #todo-vulkan
	return nullptr;
}

DescriptorHeap* VulkanDevice::createDescriptorHeap(const DescriptorHeapDesc& inDesc)
{
	VkDescriptorPoolSize poolSize{
		// #wip-descriptor: I'll have to revisit here for volatile heaps (CBV_SRV_UAV)
		.type            = into_vk::descriptorPoolType(inDesc.type),
		// #todo-vulkan: Watch out for VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK
		// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorPoolSize.html
		.descriptorCount = inDesc.numDescriptors,
	};

	VkDescriptorPoolCreateInfo createInfo{
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext         = nullptr,
		.flags         = (VkDescriptorPoolCreateFlagBits)0,
		.maxSets       = inDesc.numDescriptors,
		// #todo-vulkan: A D3D12 descriptor heap allows only one descriptor type,
		// but Vulkan descriptor pool allows multiple types.
		.poolSizeCount = 1,
		.pPoolSizes    = &poolSize,
	};

	VkDescriptorPool vkDescriptorPool = VK_NULL_HANDLE;
	VkResult ret = vkCreateDescriptorPool(vkDevice, &createInfo, nullptr, &vkDescriptorPool);
	CHECK(ret == VK_SUCCESS);

	return new VulkanDescriptorPool(inDesc, vkDescriptorPool);
}

ConstantBufferView* VulkanDevice::createCBV(Buffer* buffer, DescriptorHeap* descriptorHeap, uint32 sizeInBytes, uint32 offsetInBytes)
{
	// #todo-vulkan
	return nullptr;
}

ShaderResourceView* VulkanDevice::createSRV(GPUResource* gpuResource, const ShaderResourceViewDesc& createParams)
{
	VulkanShaderResourceView* srv = nullptr;

	if (createParams.viewDimension == ESRVDimension::Buffer)
	{
		// #todo-vulkan
	}
	else if (createParams.viewDimension == ESRVDimension::Texture2D)
	{
		VkImageViewCreateInfo createInfo{
			.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext            = nullptr,
			.flags            = 0, // VkImageViewCreateFlags
			.image            = (VkImage)(gpuResource->getRawResource()),
			.viewType         = into_vk::imageViewType(createParams.viewDimension),
			.format           = into_vk::pixelFormat(createParams.format),
			.components       = VkComponentSwizzle{},
			.subresourceRange = VkImageSubresourceRange{
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, // #todo-vulkan: Consier depthstencil case
				.baseMipLevel   = createParams.texture2D.mostDetailedMip,
				.levelCount     = createParams.texture2D.mipLevels,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			}
		};

		VkImageView vkImageView = VK_NULL_HANDLE;
		VkResult vkRet = vkCreateImageView(vkDevice, &createInfo, nullptr, &vkImageView);
		CHECK(vkRet == VK_SUCCESS);

		srv = new VulkanShaderResourceView(gpuResource, vkImageView);
	}
	else
	{
		// #todo-vulkan
		CHECK_NO_ENTRY();
	}

	return srv;
}

UnorderedAccessView* VulkanDevice::createUAV(GPUResource* gpuResource, const UnorderedAccessViewDesc& createParams)
{
	// #todo-vulkan
	return nullptr;
}

CommandSignature* VulkanDevice::createCommandSignature(const CommandSignatureDesc& inDesc, RootSignature* inRootSignature)
{
	// #todo-vulkan
	return nullptr;
}

IndirectCommandGenerator* VulkanDevice::createIndirectCommandGenerator(const CommandSignatureDesc& inDesc, uint32 maxCommandCount)
{
	// #todo-vulkan
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

void VulkanDevice::beginVkDebugMarker(
	VkCommandBuffer& cmdBuffer,
	const char* debugName,
	uint32 color /*= 0x000000*/)
{
	if (canEnableDebugMarker)
	{
		float a = color != 0 ? 1.0f : 0.0f;
		float r = (float)((color >> 16) & 0xff) / 255.0f;
		float g = (float)((color >> 8) & 0xff) / 255.0f;
		float b = (float)(color & 0xff) / 255.0f;

		VkDebugMarkerMarkerInfoEXT debugMarker{
			.sType       = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT,
			.pNext       = nullptr,
			.pMarkerName = debugName,
			.color       = {r, g, b, a},
		};
		vkCmdDebugMarkerBegin(cmdBuffer, &debugMarker);
	}
}

void VulkanDevice::endVkDebugMarker(VkCommandBuffer& cmdBuffer)
{
	if (canEnableDebugMarker)
	{
		vkCmdDebugMarkerEnd(cmdBuffer);
	}
}

void VulkanDevice::setObjectDebugName(
	VkDebugReportObjectTypeEXT objectType,
	uint64 objectHandle,
	const char* debugName)
{
	if (canEnableDebugMarker)
	{
		VkDebugMarkerObjectNameInfoEXT info{
			.sType       = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT,
			.pNext       = nullptr,
			.objectType  = objectType,
			.object      = objectHandle,
			.pObjectName = debugName,
		};
		vkDebugMarkerSetObjectName(vkDevice, &info);
	}
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
			if (0 == strcmp(layerName, layerProperties.layerName))
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
