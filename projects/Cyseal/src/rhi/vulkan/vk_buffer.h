#pragma once

#if COMPILE_BACKEND_VULKAN

#include "rhi/gpu_resource.h"
#include <vulkan/vulkan_core.h>

class RenderDevice;

class VulkanVertexBuffer : public VertexBuffer
{
public:
	~VulkanVertexBuffer();

	//~ BEGIN GPUResource interface
	virtual void* getRawResource() const override { return vkBuffer; }
	//~ END GPUResource interface

	virtual void initialize(uint32 sizeInBytes) override;

	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) override;

	virtual uint32 getVertexCount() const override { return vertexCount; };

	virtual uint64 getBufferOffsetInBytes() const override { return offsetInDefaultBuffer; }

	VkBuffer getVkBuffer() const { return vkBuffer; }

private:
	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vkBufferMemory = VK_NULL_HANDLE;
	VkDeviceSize vkBufferSize = 0;

	uint32 vertexCount = 0;
	uint64 offsetInDefaultBuffer = 0;
};

class VulkanIndexBuffer : public IndexBuffer
{
public:
	//~ BEGIN GPUResource interface
	virtual void* getRawResource() const override { return vkBuffer; }
	//~ END GPUResource interface

	virtual void initialize(uint32 sizeInBytes, EPixelFormat format) override;

	virtual void initializeWithinPool(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) override;

	virtual uint32 getIndexCount() const override { return indexCount; }
	virtual EPixelFormat getIndexFormat() const override { return indexFormat; }

	virtual uint64 getBufferOffsetInBytes() const override { return offsetInDefaultBuffer; }

	VkIndexType getIndexType() const { return vkIndexType; }
	VkBuffer getVkBuffer() const { return vkBuffer; }

private:
	EPixelFormat indexFormat = EPixelFormat::R32_UINT;
	uint32 indexCount = 0;
	uint64 offsetInDefaultBuffer = 0;

	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vkBufferMemory = VK_NULL_HANDLE;
	VkDeviceSize vkBufferSize = 0;
	VkIndexType vkIndexType = VK_INDEX_TYPE_MAX_ENUM;
};

#endif // COMPILE_BACKEND_VULKAN
