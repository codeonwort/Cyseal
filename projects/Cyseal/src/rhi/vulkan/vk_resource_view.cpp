#include "vk_resource_view.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"


VulkanRenderTargetView::~VulkanRenderTargetView()
{
	vkDestroyImageView(getVkDevice(), vkImageView, nullptr);
}

VulkanDepthStencilView::~VulkanDepthStencilView()
{
	vkDestroyImageView(getVkDevice(), vkImageView, nullptr);
}

VulkanShaderResourceView::~VulkanShaderResourceView()
{
	if (!bIsBufferView)
	{
		vkDestroyImageView(getVkDevice(), vkImageView, nullptr);
	}
}


VulkanUnorderedAccessView::~VulkanUnorderedAccessView()
{
	if (!bIsBufferView)
	{
		vkDestroyImageView(getVkDevice(), vkImageView, nullptr);
	}
}

void VulkanConstantBufferView::writeToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes)
{
	// #wip-buffer: VulkanConstantBufferView::writeToGPU
	CHECK_NO_ENTRY();
}

#endif // COMPILE_BACKEND_VULKAN
