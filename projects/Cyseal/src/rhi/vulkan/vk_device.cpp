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
	{
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
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceFeatures deviceFeatures{};
		deviceFeatures.samplerAnisotropy = VK_TRUE;

		std::vector<const char*> enabledExtensions(
			REQUIRED_DEVICE_EXTENSIONS.begin(),
			REQUIRED_DEVICE_EXTENSIONS.end());
		if (canEnableDebugMarker)
		{
			enabledExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
		}

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.queueCreateInfoCount = static_cast<uint32>(queueCreateInfos.size());
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = (uint32)enabledExtensions.size();
		createInfo.ppEnabledExtensionNames = enabledExtensions.data();
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

	// Support debug marker
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
	// #todo-vulkan-wip: Needs VkDescriptorSetLayout, and VkDescriptorSet first.
	VkPipelineLayoutCreateInfo desc{};
	desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	desc.setLayoutCount = 1;
	desc.pSetLayouts = 0; // #todo-vulkan-wip
	desc.pushConstantRangeCount = 0;
	desc.pPushConstantRanges = nullptr; // #todo-vulkan-wip: Push constant

	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkResult ret = vkCreatePipelineLayout(vkDevice, &desc, nullptr, &vkPipelineLayout);
	CHECK(ret == VK_SUCCESS);

	return new VulkanPipelineLayout(vkPipelineLayout);
}

PipelineState* VulkanDevice::createGraphicsPipelineState(const GraphicsPipelineDesc& inDesc)
{
	// #todo-vulkan-wip: PSO conversion
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	VkRenderPass vkRenderPass = VK_NULL_HANDLE;

#if 0
	// VkRenderPass
	{
		std::vector<VkAttachmentDescription> colorAttachments(inDesc.numRenderTargets);
		for (uint32 i = 0; i < inDesc.numRenderTargets; ++i)
		{
			colorAttachments[i].format = into_vk::pixelFormat(inDesc.rtvFormats[i]);
			colorAttachments[i].flags = 0; // VkAttachmentDescriptionFlags
			// #todo-vulkan: Vulkan allows different sample counts between color attachments?
			// DX12 requires same count for all attachments:
			// https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ns-dxgicommon-dxgi_sample_desc
			colorAttachments[i].samples = into_vk::sampleCount(inDesc.sampleDesc.count);
			// #todo-vulkan: loadOp, storeOp for color attachment
			colorAttachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			// #todo-vulkan: initialLayout, finalLayout for color attachment
			colorAttachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = into_vk::pixelFormat(inDesc.dsvFormat);
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		// #todo-vulkan: initialLayout, finalLayout for depth attachment
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		std::vector<VkAttachmentReference> colorAttachmentRef(inDesc.numRenderTargets);
		for (uint32 i = 0; i < inDesc.numRenderTargets; ++i)
		{
			colorAttachmentRef[i].attachment = i;
			colorAttachmentRef[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		VkAttachmentReference depthAttachmentRef = {};
		depthAttachmentRef.attachment = inDesc.numRenderTargets; // Binding point right after color attachments
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.colorAttachmentCount = inDesc.numRenderTargets;
		subpass.pColorAttachments = colorAttachmentRef.data();
		subpass.pDepthStencilAttachment = inDesc.depthstencilDesc.depthEnable ? &depthAttachmentRef : nullptr;

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

		VkRenderPassCreateInfo renderPassDesc{};
		renderPassDesc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassDesc.attachmentCount = (uint32)attachmentDesc.size();
		renderPassDesc.pAttachments = attachmentDesc.data();
		renderPassDesc.subpassCount = 1;
		renderPassDesc.pSubpasses = &subpass;
		renderPassDesc.dependencyCount = 0;
		renderPassDesc.pDependencies = nullptr;

		VkResult ret = vkCreateRenderPass(
			vkDevice, &renderPassDesc, nullptr, &vkRenderPass);
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
		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = shaderWrapper->getVkShaderStage();
		stageInfo.module = shaderWrapper->getVkShaderModule();
		stageInfo.pName = shaderWrapper->getEntryPoint();
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
	VkPipelineVertexInputStateCreateInfo vertexInputDesc{};
	vertexInputDesc.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputDesc.vertexBindingDescriptionCount = (uint32)inputBindings.size();
	vertexInputDesc.pVertexBindingDescriptions = inputBindings.data();
	vertexInputDesc.vertexAttributeDescriptionCount = (uint32)inputAttributes.size();
	vertexInputDesc.pVertexAttributeDescriptions = inputAttributes.data();

	// pInputAssemblyState
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = into_vk::primitiveTopologyType(inDesc.primitiveTopologyType);
	inputAssembly.primitiveRestartEnable = VK_FALSE; // #todo-vulkan: Primitive Restart

	// pRasterizationState
	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = inDesc.rasterizerDesc.depthClipEnable;
	rasterizer.rasterizerDiscardEnable = VK_FALSE; // #todo-vulkan: rasterizerDiscardEnable
	rasterizer.polygonMode = into_vk::polygonMode(inDesc.rasterizerDesc.fillMode);
	rasterizer.cullMode = into_vk::cullMode(inDesc.rasterizerDesc.cullMode);
	rasterizer.frontFace = inDesc.rasterizerDesc.frontCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = (0 != inDesc.rasterizerDesc.depthBias);
	rasterizer.depthBiasConstantFactor = (float)inDesc.rasterizerDesc.depthBias;
	rasterizer.depthBiasClamp = inDesc.rasterizerDesc.depthBiasClamp;
	rasterizer.depthBiasSlopeFactor = inDesc.rasterizerDesc.slopeScaledDepthBias;
	rasterizer.lineWidth = 1.0f; // #todo-crossapi: vk-only?

	// pMultisampleState
	// #todo-vulkan: Support multisampling
	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;

	// pDepthStencilState
	VkPipelineDepthStencilStateCreateInfo depthStencil
		= into_vk::depthstencilDesc(inDesc.depthstencilDesc);

	// pColorBlendState
	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(inDesc.numRenderTargets);
	for (uint32 i = 0; i < inDesc.numRenderTargets; ++i)
	{
		const RenderTargetBlendDesc& inBlend = inDesc.blendDesc.renderTarget[i];
		colorBlendAttachments[i].colorWriteMask = into_vk::colorWriteMask(inBlend.renderTargetWriteMask);
		colorBlendAttachments[i].blendEnable = inBlend.blendEnable;
		colorBlendAttachments[i].srcColorBlendFactor = into_vk::blendFactor(inBlend.srcBlend);
		colorBlendAttachments[i].dstColorBlendFactor = into_vk::blendFactor(inBlend.destBlend);
		colorBlendAttachments[i].colorBlendOp = into_vk::blendOp(inBlend.blendOp);
		colorBlendAttachments[i].srcAlphaBlendFactor = into_vk::blendFactor(inBlend.srcBlendAlpha);
		colorBlendAttachments[i].dstAlphaBlendFactor = into_vk::blendFactor(inBlend.destBlendAlpha);
		colorBlendAttachments[i].alphaBlendOp = into_vk::blendOp(inBlend.blendOpAlpha);
	}
	// #todo-vulkan: Independent blend state is an extension in Vulkan
	VkPipelineColorBlendStateCreateInfo colorBlendDesc{};
	colorBlendDesc.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendDesc.logicOpEnable = inDesc.blendDesc.renderTarget[0].logicOpEnable;
	colorBlendDesc.logicOp = into_vk::logicOp(inDesc.blendDesc.renderTarget[0].logicOp);
	colorBlendDesc.attachmentCount = (uint32)colorBlendAttachments.size();
	colorBlendDesc.pAttachments = colorBlendAttachments.data();
	// #todo-vulkan: Dynamic blend factor
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkCmdSetBlendConstants.html
	colorBlendDesc.blendConstants[0] = 0.0f;
	colorBlendDesc.blendConstants[1] = 0.0f;
	colorBlendDesc.blendConstants[2] = 0.0f;
	colorBlendDesc.blendConstants[3] = 0.0f;

	// pDynamicState
	// #todo-vulkan: DX12 always set them dynamically via RSSet~~ methods,
	// so we make them always dynamic in Vulkan backend also. Some render passes
	// would have fixed viewport and scissor, but we ignore the tiny overhead for them.
	std::array<VkDynamicState, 2> dynamicStates = {
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.dynamicStateCount = (uint32_t)dynamicStates.size();
	dynamicStateInfo.pDynamicStates = dynamicStates.data();

	// layout
	// #todo-vulkan-wip: Take this as parameter
	RootSignature* inRootSignature = nullptr;
	VkPipelineLayout vkPipelineLayout = static_cast<VulkanPipelineLayout*>(inRootSignature)->getVkPipelineLayout();

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = (uint32)shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputDesc;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = nullptr; // Always dynamic state in my engine.
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlendDesc;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = vkPipelineLayout;
	pipelineInfo.renderPass = vkRenderPass;
	pipelineInfo.subpass = 0; // index of the subpass in the render pass where this pipeline will be used.
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

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

RaytracingPipelineStateObject* VulkanDevice::createRaytracingPipelineStateObject(const RaytracingPipelineStateObjectDesc& desc)
{
	// #todo-vulkan
	return nullptr;
}

RaytracingShaderTable* VulkanDevice::createRaytracingShaderTable(RaytracingPipelineStateObject* RTPSO, uint32 numShaderRecords, uint32 rootArgumentSize, const wchar_t* debugName)
{
	// #todo-vulkan
	return nullptr;
}

DescriptorHeap* VulkanDevice::createDescriptorHeap(const DescriptorHeapDesc& inDesc)
{
	VkDescriptorPoolSize poolSize{};
	// #todo-vulkan-wip: I'll have to revisit here for volatile heaps (CBV_SRV_UAV)
	poolSize.type = into_vk::descriptorPoolType(inDesc.type);
	// #todo-vulkan: Watch out for VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorPoolSize.html
	poolSize.descriptorCount = inDesc.numDescriptors;

	VkDescriptorPoolCreateInfo desc{};
	desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	// #todo-vulkan: A D3D12 descriptor heap allows only one descriptor type,
	// but Vulkan descriptor pool allows multiple types.
	desc.poolSizeCount = 1;
	desc.pPoolSizes = &poolSize;
	desc.maxSets = inDesc.numDescriptors;

	VkDescriptorPool vkDescriptorPool = VK_NULL_HANDLE;
	VkResult ret = vkCreateDescriptorPool(vkDevice, &desc, nullptr, &vkDescriptorPool);
	CHECK(ret == VK_SUCCESS);

	VulkanDescriptorPool* heap = new VulkanDescriptorPool(inDesc, vkDescriptorPool);
	return heap;
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
			.flags            = 0,
			.image            = (VkImage)(gpuResource->getRawResource()),
			.viewType         = into_vk::imageViewType(createParams.viewDimension),
			.format           = into_vk::pixelFormat(createParams.format),
			.components       = VkComponentSwizzle{},
			.subresourceRange = VkImageSubresourceRange{
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
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

		VkDebugMarkerMarkerInfoEXT debugMarker{};
		debugMarker.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		debugMarker.pMarkerName = debugName;
		debugMarker.color[0] = r;
		debugMarker.color[1] = g;
		debugMarker.color[2] = b;
		debugMarker.color[3] = a;
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
		VkDebugMarkerObjectNameInfoEXT info{};
		info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
		info.objectType = objectType;
		info.object = objectHandle;
		info.pObjectName = debugName;
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
