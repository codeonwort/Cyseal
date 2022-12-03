#pragma once

#if COMPILE_BACKEND_VULKAN

#include "rhi/gpu_resource_view.h"
#include <vulkan/vulkan_core.h>

class VulkanRenderTargetView : public RenderTargetView
{
public:
	VulkanRenderTargetView(VkImageView inHandle) : handle(inHandle) {}
	VkImageView getRaw() const { return handle; }
private:
	VkImageView handle = VK_NULL_HANDLE;
};

class VulkanDepthStencilView : public DepthStencilView
{
public:
	VulkanDepthStencilView(VkImageView inHandle) : handle(inHandle) {}
	VkImageView getRaw() const { return handle; }
private:
	VkImageView handle = VK_NULL_HANDLE;
};

class VulkanShaderResourceView : public ShaderResourceView
{
public:
	VulkanShaderResourceView(GPUResource* inOwner, VkImageView inVkImageView)
		: ShaderResourceView(inOwner, /*inSourceHeap*/nullptr, /*inDescriptorIndex*/0xffffffff)
		, vkImageView(inVkImageView)
	{}
	VkImageView getVkImageView() const { return vkImageView; }
private:
	VkImageView vkImageView = VK_NULL_HANDLE;
};

class VulkanUnorderedAccessView : public UnorderedAccessView
{
public:
	VulkanUnorderedAccessView(
		GPUResource* inOwner,
		DescriptorHeap* inSourceHeap,
		uint32 inDescriptorIndex,
		VkImageView inHandle)
		: UnorderedAccessView(inOwner, inSourceHeap, inDescriptorIndex)
		, handle(inHandle)
	{}
	VkImageView getRaw() const { return handle; }
private:
	VkImageView handle = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
