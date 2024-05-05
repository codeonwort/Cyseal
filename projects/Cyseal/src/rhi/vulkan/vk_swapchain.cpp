#include "vk_swapchain.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_utils.h"
#include "vk_resource_view.h"
#include "rhi/gpu_resource_binding.h"
#include "core/platform.h"
#include "core/assertion.h"
#include "util/logging.h"

#include <array>

VulkanSwapchain::VulkanSwapchain()
{
	swapchainImageCount = 0;
}

void VulkanSwapchain::preinitialize(RenderDevice* renderDevice)
{
	deviceWrapper = static_cast<VulkanDevice*>(renderDevice);
	auto physicalDevice = deviceWrapper->vkPhysicalDevice;
	SwapChainSupportDetails supportDetails = deviceWrapper->querySwapChainSupport(physicalDevice);

	uint32 imageCount = std::max(2u, supportDetails.capabilities.minImageCount);
	// maxImageCount = 0 means there's no limit besides memory requirements
	if (supportDetails.capabilities.maxImageCount > 0 && imageCount > supportDetails.capabilities.maxImageCount)
	{
		imageCount = supportDetails.capabilities.maxImageCount;
	}
	swapchainImageCount = imageCount;
}

void VulkanSwapchain::initialize(
	RenderDevice* renderDevice,
	void* nativeWindowHandle,
	uint32 width,
	uint32 height)
{
	CHECK(deviceWrapper == renderDevice);

	backbufferWidth = width;
	backbufferHeight = height;
	backbufferFormat = deviceWrapper->getBackbufferFormat();
	backbufferDepthFormat = deviceWrapper->getBackbufferDepthFormat();

	SwapChainSupportDetails swapChainSupport = deviceWrapper->querySwapChainSupport(deviceWrapper->vkPhysicalDevice);
	VkSurfaceFormatKHR surfaceFormat = deviceWrapper->chooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode = deviceWrapper->chooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = deviceWrapper->chooseSwapExtent(swapChainSupport.capabilities, width, height);

	CYLOG(LogVulkan, Log, L"Create swapchain images");
	{
		QueueFamilyIndices indices = findQueueFamilies(deviceWrapper->vkPhysicalDevice, deviceWrapper->vkSurface);
		uint32 queueFamilyIndices[] = { static_cast<uint32>(indices.graphicsFamily), static_cast<uint32>(indices.presentFamily) };
		
		VkSharingMode imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // Best performance
		uint32 queueFamilyIndexCount = 0; // Optional
		const uint32* pQueueFamilyIndices = nullptr; // Optional
		if (indices.graphicsFamily != indices.presentFamily)
		{
			imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			queueFamilyIndexCount = 2;
			pQueueFamilyIndices = queueFamilyIndices;
		}

		VkSwapchainCreateInfoKHR createInfo{
			.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.pNext                 = nullptr,
			.flags                 = 0, // VkSwapchainCreateFlagsKHR
			.surface               = deviceWrapper->vkSurface,
			.minImageCount         = swapchainImageCount,
			.imageFormat           = surfaceFormat.format,
			.imageColorSpace       = surfaceFormat.colorSpace,
			.imageExtent           = extent,
			.imageArrayLayers      = 1, // 1 unless developing a stereoscopic 3D application
			.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.imageSharingMode      = imageSharingMode,
			.queueFamilyIndexCount = queueFamilyIndexCount,
			.pQueueFamilyIndices   = pQueueFamilyIndices,
			.preTransform          = swapChainSupport.capabilities.currentTransform,
			.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode           = presentMode,
			.clipped               = VK_TRUE,
			.oldSwapchain          = VK_NULL_HANDLE,
		};

		VkResult ret = vkCreateSwapchainKHR(deviceWrapper->vkDevice, &createInfo, nullptr, &swapchainKHR);
		CHECK(ret == VK_SUCCESS);

		vkGetSwapchainImagesKHR(deviceWrapper->vkDevice, swapchainKHR, &swapchainImageCount, nullptr);
		std::vector<VkImage> vkSwapchainImages(swapchainImageCount, VK_NULL_HANDLE);
		vkGetSwapchainImagesKHR(deviceWrapper->vkDevice, swapchainKHR, &swapchainImageCount, vkSwapchainImages.data());
		
		swapchainImages.initialize(swapchainImageCount);
		for (uint32 i = 0; i < swapchainImageCount; ++i)
		{
			swapchainImages[i] = makeUnique<VulkanSwapchainImage>(vkSwapchainImages[i]);
			std::wstring debugName = std::wstring(L"SwapchainImage_") + std::to_wstring(i);
			swapchainImages[i]->setDebugName(debugName.c_str());
		}

		swapchainImageFormat = surfaceFormat.format;
		swapchainExtent = extent;
	}

	CYLOG(LogVulkan, Log, L"> Create image views (RTVs) for swapchain images");
	{
		// CAUTION: gDescriptorHeaps is not initialized yet.
		DescriptorHeapDesc heapDesc{
			.type           = EDescriptorHeapType::RTV,
			.numDescriptors = swapchainImageCount,
			.flags          = EDescriptorHeapFlags::None,
			.nodeMask       = 0,
		};
		heapRTV = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(heapDesc));

		swapchainImageViews.initialize((uint32)swapchainImages.size());
		for (size_t i = 0; i < swapchainImages.size(); ++i)
		{
			// #wip: surfaceFormat.format is bgra8, backbufferFormat is rgba8
			EPixelFormat rtvFormat = backbufferFormat;
			if (rtvFormat == EPixelFormat::R8G8B8A8_UNORM)
			{
				rtvFormat = EPixelFormat::B8G8R8A8_UNORM;
			}

			auto rtv = gRenderDevice->createRTV(swapchainImages.at(i), heapRTV.get(),
				RenderTargetViewDesc{
					.format        = rtvFormat,
					.viewDimension = ERTVDimension::Texture2D,
					.texture2D     = Texture2DRTVDesc {.mipSlice = 0, .planeSlice = 0 },
				}
			);
			swapchainImageViews[i] = UniquePtr<RenderTargetView>(rtv);
		}
	}

	// DearImgui is rendered directly to backbuffer so we need them.
	CYLOG(LogVulkan, Log, L"> Create render pass for backbuffer");
	{
		VkAttachmentDescription colorAttachment{
			.flags          = (VkAttachmentDescriptionFlags)0,
			.format         = swapchainImageFormat,
			.samples        = VK_SAMPLE_COUNT_1_BIT,
			.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		};

		VkAttachmentReference colorAttachmentRef{
			.attachment = 0,
			.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentDescription depthAttachment{
			.flags          = (VkAttachmentDescriptionFlags)0,
			.format         = findDepthFormat(deviceWrapper->vkPhysicalDevice),
			.samples        = VK_SAMPLE_COUNT_1_BIT,
			.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference depthAttachmentRef{
			.attachment = 1,
			.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass{
			.flags                   = (VkSubpassDescriptionFlags)0,
			.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount    = 0,
			.pInputAttachments       = nullptr,
			.colorAttachmentCount    = 1,
			.pColorAttachments       = &colorAttachmentRef,
			.pResolveAttachments     = nullptr,
			.pDepthStencilAttachment = &depthAttachmentRef,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments    = nullptr,
		};

		VkSubpassDependency dependency{
			.srcSubpass      = VK_SUBPASS_EXTERNAL,
			.dstSubpass      = 0,
			.srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask   = 0,
			.dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = (VkDependencyFlags)0,
		};

		std::array<VkAttachmentDescription, 2> attachments = {
			colorAttachment, depthAttachment
		};

		VkRenderPassCreateInfo renderPassInfo{
			.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.pNext           = nullptr,
			.flags           = (VkRenderPassCreateFlags)0,
			.attachmentCount = static_cast<uint32>(attachments.size()),
			.pAttachments    = attachments.data(),
			.subpassCount    = 1,
			.pSubpasses      = &subpass,
			.dependencyCount = 1,
			.pDependencies   = &dependency,
		};

		VkResult ret = vkCreateRenderPass(
			deviceWrapper->vkDevice,
			&renderPassInfo,
			nullptr,
			&backbufferRenderPass);
		CHECK(ret == VK_SUCCESS);
	}

	CYLOG(LogVulkan, Log, L"> Create depth resources for backbuffer");
	{
		VkFormat depthFormat = findDepthFormat(deviceWrapper->vkPhysicalDevice);
	
		createImage(
			deviceWrapper->vkPhysicalDevice,
			deviceWrapper->vkDevice,
			swapchainExtent.width,
			swapchainExtent.height,
			depthFormat,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			depthImage, depthImageMemory);
	
		depthImageView = createImageView(
			deviceWrapper->vkDevice,
			depthImage,
			depthFormat,
			VK_IMAGE_ASPECT_DEPTH_BIT);
	
		transitionImageLayout(
			deviceWrapper->vkDevice,
			deviceWrapper->getTempCommandPool(),
			deviceWrapper->vkGraphicsQueue,
			depthImage, depthFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	CYLOG(LogVulkan, Log, L"> Create framebuffers for backbuffer");
	{
		swapchainFramebuffers.resize(swapchainImageViews.size());

		for (size_t i = 0; i < swapchainImageViews.size(); ++i)
		{
			VkImageView colorView = static_cast<VulkanRenderTargetView*>(swapchainImageViews.at(i))->getVkImageView();

			std::array<VkImageView, 2> attachments = { colorView, depthImageView };

			VkFramebufferCreateInfo framebufferInfo{
				.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.pNext           = nullptr,
				.flags           = (VkFramebufferCreateFlags)0,
				.renderPass      = backbufferRenderPass,
				.attachmentCount = static_cast<uint32>(attachments.size()),
				.pAttachments    = attachments.data(),
				.width           = swapchainExtent.width,
				.height          = swapchainExtent.height,
				.layers          = 1,
			};

			VkResult ret = vkCreateFramebuffer(
				deviceWrapper->vkDevice,
				&framebufferInfo,
				nullptr,
				&swapchainFramebuffers[i]);
			CHECK(ret == VK_SUCCESS);
		}
	}
}

void VulkanSwapchain::resize(uint32 newWidth, uint32 newHeight)
{
	// #todo-vulkan: VulkanSwapchain::resize
	CHECK_NO_ENTRY();
}

void VulkanSwapchain::present()
{
	VkSemaphore waitSemaphores[] = { deviceWrapper->getVkRenderFinishedSemaphore() };
	VkSwapchainKHR swapchains[] = { swapchainKHR };
	uint32 swapchainIndices[] = { currentBackbufferIx };

	VkPresentInfoKHR presentInfo{
		.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext              = nullptr,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores    = waitSemaphores,
		.swapchainCount     = 1,
		.pSwapchains        = swapchains,
		.pImageIndices      = swapchainIndices,
		.pResults           = nullptr,
	};

	VkResult ret = vkQueuePresentKHR(deviceWrapper->getVkPresentQueue(), &presentInfo);
	if (ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR)
	{
		resize(backbufferWidth, backbufferHeight);
	}
	else
	{
		CHECK(ret == VK_SUCCESS);
	}
}

void VulkanSwapchain::swapBackbuffer()
{
	VkDevice vkDevice = deviceWrapper->getRaw();
	VkResult ret = vkAcquireNextImageKHR(
		vkDevice,
		swapchainKHR,
		UINT64_MAX,
		deviceWrapper->getVkSwapchainImageAvailableSemaphore(),
		VK_NULL_HANDLE,
		&currentBackbufferIx);
	CHECK(ret == VK_SUCCESS);
}

uint32 VulkanSwapchain::getCurrentBackbufferIndex() const
{
	return currentBackbufferIx;
}

GPUResource* VulkanSwapchain::getSwapchainBuffer(uint32 ix) const
{
	return swapchainImages.at(ix);
}

RenderTargetView* VulkanSwapchain::getSwapchainBufferRTV(uint32 ix) const
{
	return swapchainImageViews.at(ix);
}

#endif // COMPILE_BACKEND_VULKAN
