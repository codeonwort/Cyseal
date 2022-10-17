#pragma once

#include "render/gpu_resource.h"
#include <vulkan/vulkan_core.h>

// #todo-vulkan: VertexBuffer
class VulkanVertexBuffer : public VertexBuffer
{
public:
	virtual void initialize(uint32 sizeInBytes) override;

	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) override;

private:
	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vkBufferMemory = VK_NULL_HANDLE;
	VkDeviceSize vkBufferSize = 0;
};

// #todo-vulkan: IndexBuffer
class VulkanIndexBuffer : public IndexBuffer
{
public:
	void initialize(uint32 sizeInBytes) override;

	virtual void initializeWithinPool(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) override;

	uint32 getIndexCount() override { return indexCount; }

private:
	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vkBufferMemory = VK_NULL_HANDLE;
	VkDeviceSize vkBufferSize = 0;
	uint32 indexCount = 0;
};
