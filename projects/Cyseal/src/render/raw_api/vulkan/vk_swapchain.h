#pragma once

#if COMPILE_BACKEND_VULKAN

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
		void*         nativeWindowHandle,
		uint32        width,
		uint32        height) override;

	virtual void present() override;
	virtual void swapBackbuffer() override;
	virtual uint32 getBufferCount() override { return swapchainImageCount; }

	virtual GPUResource* getCurrentBackbuffer() const override;
	virtual RenderTargetView* getCurrentBackbufferRTV() const override;

private:
	// #todo-vulkan: Init subroutines here

private:
	VkSwapchainKHR swapchainKHR;
	VkExtent2D swapchainExtent;
	uint32 swapchainImageCount;

	std::vector<VkImage> swapchainImages;
	VkFormat swapchainImageFormat;
	std::vector<VkImageView> swapchainImageViews;

	VkRenderPass backbufferRenderPass;
	std::vector<VkFramebuffer> swapchainFramebuffers;

	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;
};

#endif // COMPILE_BACKEND_VULKAN
