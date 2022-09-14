#include "vk_swapchain.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_utils.h"
#include "core/assertion.h"
#include "util/logging.h"
#include <array>

// #todo-vulkan: Use this and remove swapchain code from VulkanDevice

VulkanSwapchain::VulkanSwapchain()
{
	swapchainImageCount = 0;
}

void VulkanSwapchain::initialize(RenderDevice* renderDevice, HWND hwnd, uint32 width, uint32 height)
{
	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(renderDevice);

	VulkanDevice::SwapChainSupportDetails swapChainSupport = deviceWrapper->querySwapChainSupport(deviceWrapper->physicalDevice);
	VkSurfaceFormatKHR surfaceFormat = deviceWrapper->chooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode = deviceWrapper->chooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = deviceWrapper->chooseSwapExtent(swapChainSupport.capabilities, width, height);

	CYLOG(LogVulkan, Log, TEXT("Create swapchain images"));
	{
		uint32 imageCount = swapChainSupport.capabilities.minImageCount + 1;
		// maxImageCount = 0 means there's no limit besides memory requirements
		if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
		{
			imageCount = swapChainSupport.capabilities.maxImageCount;
		}
		swapchainImageCount = imageCount;

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = deviceWrapper->surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1; // 1 unless developming a stereoscopic 3D application
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		VulkanDevice::QueueFamilyIndices indices = deviceWrapper->findQueueFamilies(deviceWrapper->physicalDevice);
		uint32 queueFamilyIndices[] = { static_cast<uint32>(indices.graphicsFamily), static_cast<uint32>(indices.presentFamily) };
		if (indices.graphicsFamily != indices.presentFamily)
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		} else
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

		VkResult ret = vkCreateSwapchainKHR(deviceWrapper->device, &createInfo, nullptr, &swapchainKHR);
		CHECK(ret == VK_SUCCESS);

		vkGetSwapchainImagesKHR(deviceWrapper->device, swapchainKHR, &imageCount, nullptr);
		swapchainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(deviceWrapper->device, swapchainKHR, &imageCount, swapchainImages.data());
		swapchainImageFormat = surfaceFormat.format;
		swapchainExtent = extent;
	}

	CYLOG(LogVulkan, Log, TEXT("> Create image views for swapchain images"));
	{
		swapchainImageViews.resize(swapchainImages.size());
		for (size_t i = 0; i < swapchainImages.size(); ++i)
		{
			swapchainImageViews[i] = createImageView(
				deviceWrapper->device,
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
		depthAttachment.format = findDepthFormat(deviceWrapper->physicalDevice);
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

		VkResult ret = vkCreateRenderPass(deviceWrapper->device, &renderPassInfo, nullptr, &backbufferRenderPass);
		CHECK(ret == VK_SUCCESS);
	}

	CYLOG(LogVulkan, Log, TEXT("> Create depth resources for backbuffer"));
	{
		VkFormat depthFormat = findDepthFormat(deviceWrapper->physicalDevice);

		createImage(deviceWrapper->physicalDevice, deviceWrapper->device,
			swapchainExtent.width, swapchainExtent.height,
			depthFormat,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			depthImage, depthImageMemory);

		depthImageView = createImageView(deviceWrapper->device, depthImage, depthFormat,
			VK_IMAGE_ASPECT_DEPTH_BIT);

		transitionImageLayout(
			deviceWrapper->device,
			deviceWrapper->commandPool,
			deviceWrapper->graphicsQueue,
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

			VkResult ret = vkCreateFramebuffer(
				deviceWrapper->device,
				&framebufferInfo,
				nullptr,
				&swapchainFramebuffers[i]);
			CHECK(ret == VK_SUCCESS);
		}
	}

	// Set inherited member variables
	//uint32 backbufferWidth;
	//uint32 backbufferHeight;
	//EPixelFormat backbufferFormat;
	//EPixelFormat backbufferDepthFormat;
}

void VulkanSwapchain::present()
{
	//
}

void VulkanSwapchain::swapBackbuffer()
{
	//
}

GPUResource* VulkanSwapchain::getCurrentBackbuffer() const
{
	return nullptr;
}

RenderTargetView* VulkanSwapchain::getCurrentBackbufferRTV() const
{
	return nullptr;
}

#endif // COMPILE_BACKEND_VULKAN
