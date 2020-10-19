#pragma once

#include "render/swap_chain.h"
#include <vector>
#include <vulkan/vulkan_core.h>

/* ------------------------------------------------------------------------------------
										NOTE

https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkRenderPass.html
	A render pass represents a collection of attachments, subpasses,
	and dependencies between the subpasses, and describes how the
	attachments are used over the course of the subpasses.
	The use of a render pass in a command buffer is a render pass instance.
------------------------------------------------------------------------------------ */

class VulkanSwapchain : public SwapChain
{
public:
	VulkanSwapchain();

	virtual void initialize(
		RenderDevice* renderDevice,
		HWND          hwnd,
		uint32_t      width,
		uint32_t      height) override;

	virtual void present() override;
	virtual void swapBackbuffer() override;

	virtual GPUResource* getCurrentBackbuffer() const override;
	virtual RenderTargetView* getCurrentBackbufferRTV() const override;

private:
	VkSwapchainKHR swapchainKHR;
	VkExtent2D swapchainExtent;

	std::vector<VkImage> swapchainImages;
	VkFormat swapchainImageFormat;
	std::vector<VkImageView> swapchainImageViews;

	VkRenderPass backbufferRenderPass;
	std::vector<VkFramebuffer> swapchainFramebuffers;

	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;
};
