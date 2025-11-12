#include "vk_resource_view.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"


VulkanRenderTargetView::~VulkanRenderTargetView()
{
	vkDestroyImageView(device->getRaw(), vkImageView, nullptr);
}

VulkanDepthStencilView::~VulkanDepthStencilView()
{
	vkDestroyImageView(device->getRaw(), vkImageView, nullptr);
}

VulkanShaderResourceView::~VulkanShaderResourceView()
{
	if (!bIsBufferView)
	{
		vkDestroyImageView(device->getRaw(), vkImageView, nullptr);
	}
}

VulkanUnorderedAccessView::~VulkanUnorderedAccessView()
{
	if (!bIsBufferView)
	{
		vkDestroyImageView(device->getRaw(), vkImageView, nullptr);
	}
}

void VulkanConstantBufferView::writeToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes)
{
	// #todo-vulkan: VulkanConstantBufferView::writeToGPU
	CHECK_NO_ENTRY();
}

#endif // COMPILE_BACKEND_VULKAN
