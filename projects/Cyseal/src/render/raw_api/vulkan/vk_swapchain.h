#pragma once

#include "render/swap_chain.h"
#include <vector>
#include <vulkan/vulkan_core.h>

/* ------------------------------------------------------
NOTE

https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkRenderPass.html
	A render pass represents a collection of attachments, subpasses,
	and dependencies between the subpasses, and describes how the
	attachments are used over the course of the subpasses.
	The use of a render pass in a command buffer is a render pass instance.
------------------------------------------------------ */

class VulkanSwapchain : public SwapChain
{
public:
	//

private:
	VkSwapchainKHR swapchainKHR;
	std::vector<VkImage> swapchainImages;
	VkFormat swapchainImageFormat;
	VkExtent2D swapchainExtent;
	std::vector<VkImageView> swapchainImageViews;
	VkRenderPass backbufferRenderPass; // Vulkan render pass is similar to OpenGL FBO and has nothing to do with my RenderPass class.
	std::vector<VkFramebuffer> swapchainFramebuffers;
	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;
};