#pragma once

#if COMPILE_BACKEND_VULKAN

#include "core/smart_pointer.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource.h"

#include <vector>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

/* ------------------------------------------------------------------------------------
										NOTE

https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkRenderPass.html
	A render pass represents a collection of attachments, subpasses,
	and dependencies between the subpasses, and describes how the
	attachments are used over the course of the subpasses.
	The use of a render pass in a command buffer is a render pass instance.
------------------------------------------------------------------------------------ */

class DescriptorHeap;
class VulkanDevice;
class VulkanRenderTargetView;

class VulkanSwapchainImage : public SwapChainImage
{
public:
	VulkanSwapchainImage(VkImage inVkImage)
		: vkImage(inVkImage)
	{
	}
	VkImage getVkImage() const
	{
		return vkImage;
	}
	virtual void* getRawResource() const override
	{
		return vkImage;
	}
	virtual void setRawResource(void* inResource) override
	{
		vkImage = reinterpret_cast<VkImage>(inResource);
	}

	// Unwanted hack due to implicit layout conversion by Vulkan API or third party modules. Outside of my control :(
	void internal_overrideLastImageLayout(EBarrierLayout layout)
	{
		auto state = internal_getLastBarrierState();
		CHECK(state.bHolistic);
		state.globalState.layoutBefore = layout;
		internal_setLastBarrierState(state);
	}

private:
	VkImage vkImage = VK_NULL_HANDLE;
};

class VulkanSwapchain : public SwapChain
{
public:
	VulkanSwapchain();

	void preinitialize(RenderDevice* renderDevice);

	virtual void initialize(
		RenderDevice* renderDevice,
		void*         nativeWindowHandle,
		uint32        width,
		uint32        height) override;

	virtual void resize(uint32 newWidth, uint32 newHeight) override;

	virtual void present() override;
	virtual void prepareBackbuffer() override;
	virtual uint32 getBufferCount() const override { return swapchainImageCount; }

	virtual uint32 getCurrentBackbufferIndex() const override;
	virtual SwapChainImage* getSwapchainBuffer(uint32 ix) const override;
	virtual RenderTargetView* getSwapchainBufferRTV(uint32 ix) const override;

	inline VkFormat getVkSwapchainImageFormat() const { return swapchainImageFormat; }
	inline VkRenderPass getVkRenderPass() const { return backbufferRenderPass; }
	inline VkFramebuffer getVkFramebuffer(uint32 ix) const { return swapchainFramebuffers[ix]; }
	inline VkSampleCountFlagBits getVkSampleCountFlagBits() const { return vkSampleCountFlagBits; }

	inline VkSemaphore internal_getCurrentImageAvailableSemaphore() const { return semaphoreInFlight; }

private:
	VulkanDevice* deviceWrapper = nullptr;

	uint32 currentBackbufferIx = 0xffffffff;
	VkSemaphore semaphoreInFlight = VK_NULL_HANDLE;
	uint32 backbufferInFlight = 0xffffffff;

	VkSwapchainKHR swapchainKHR = VK_NULL_HANDLE;
	VkExtent2D swapchainExtent = VkExtent2D{ 0,0 };
	uint32 swapchainImageCount = 0;
	// #todo-vulkan: backbuffer sample count
	// Also gotta do something with SwapChain::get4xMSAAQuality()
	VkSampleCountFlagBits vkSampleCountFlagBits = VK_SAMPLE_COUNT_1_BIT;

	VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
	BufferedUniquePtr<VulkanSwapchainImage> swapchainImages;

	UniquePtr<DescriptorHeap> heapRTV;
	BufferedUniquePtr<RenderTargetView> swapchainImageViews;

	VkRenderPass backbufferRenderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> swapchainFramebuffers;

	VkImage depthImage = VK_NULL_HANDLE;
	VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
	VkImageView depthImageView = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
