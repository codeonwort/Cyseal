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
	VulkanShaderResourceView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex, VkImageView inVkImageView)
		: ShaderResourceView(inOwner, inSourceHeap, inDescriptorIndex)
		, vkImageView(inVkImageView)
		, bIsBufferView(false)
	{}
	VulkanShaderResourceView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex, VkBufferView inVkBufferView)
		: ShaderResourceView(inOwner, inSourceHeap, inDescriptorIndex)
		, vkBufferView(inVkBufferView)
		, bIsBufferView(true)
	{}
	inline bool isBufferView() const { return bIsBufferView; }
	inline VkBufferView getVkBufferView() const { return vkBufferView; }
	inline VkImageView getVkImageView() const { return vkImageView; }
private:
	const bool bIsBufferView;
	VkBufferView vkBufferView = VK_NULL_HANDLE;
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
