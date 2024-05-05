#pragma once

#if COMPILE_BACKEND_VULKAN

#include "rhi/gpu_resource_view.h"
#include <vulkan/vulkan_core.h>

class VulkanBuffer;

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
	VulkanDepthStencilView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex, VkImageView inHandle)
		: DepthStencilView(inOwner, inSourceHeap, inDescriptorIndex)
		, handle(inHandle)
	{}

	~VulkanDepthStencilView();

	VkImageView getRaw() const { return handle; }
private:
	VkImageView handle = VK_NULL_HANDLE;
};

class VulkanConstantBufferView : public ConstantBufferView
{
public:
	VulkanConstantBufferView(VkBuffer inHandle, uint32 inSizeInBytes, uint32 inOffsetInBytes, DescriptorHeap* inDescriptorHeap, uint32 inDescriptorIndex)
		: handle(inHandle), sizeInBytes(inSizeInBytes), offsetInBytes(inOffsetInBytes)
		, descriptorHeap(inDescriptorHeap), descriptorIndex(inDescriptorIndex)
	{}

	virtual void writeToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes) override;

	virtual DescriptorHeap* getSourceHeap() const override { return descriptorHeap; }
	virtual uint32 getDescriptorIndexInHeap() const override { return descriptorIndex; }
	
	VkBuffer getVkBuffer() const { return handle; }

private:
	VkBuffer        handle          = VK_NULL_HANDLE;
	uint32          sizeInBytes     = 0;
	uint32          offsetInBytes   = 0;
	DescriptorHeap* descriptorHeap  = nullptr;
	uint32          descriptorIndex = 0xffffffff;
};

class VulkanShaderResourceView : public ShaderResourceView
{
public:
	VulkanShaderResourceView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex, VkImageView inVkImageView)
		: ShaderResourceView(inOwner, inSourceHeap, inDescriptorIndex)
		, vkImageView(inVkImageView)
		, bIsBufferView(false)
	{}
	VulkanShaderResourceView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex, VkBuffer inVkBuffer)
		: ShaderResourceView(inOwner, inSourceHeap, inDescriptorIndex)
		, vkBuffer(inVkBuffer)
		, bIsBufferView(true)
	{}
	~VulkanShaderResourceView();
	inline bool isBufferView() const { return bIsBufferView; }
	inline VkBuffer getBufferWrapper() const { return vkBuffer; }
	inline VkImageView getVkImageView() const { return vkImageView; }
private:
	const bool bIsBufferView;
	VkBuffer vkBuffer = VK_NULL_HANDLE;
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
