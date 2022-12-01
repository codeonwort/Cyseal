#pragma once

#if COMPILE_BACKEND_VULKAN

#include "render/gpu_resource.h"
#include <vulkan/vulkan_core.h>

class VulkanVertexBuffer : public VertexBuffer
{
public:
	virtual void initialize(uint32 sizeInBytes) override;

	virtual void initializeWithinPool(
		VertexBufferPool* pool,
		uint64 offsetInPool,
		uint32 sizeInBytes) override;

	virtual void updateData(
		RenderCommandList* commandList,
		void* data,
		uint32 strideInBytes) override;

	VkBuffer getVkBuffer() const { return vkBuffer; }

private:
	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vkBufferMemory = VK_NULL_HANDLE;
	VkDeviceSize vkBufferSize = 0;
};

class VulkanIndexBuffer : public IndexBuffer
{
public:
	virtual void initialize(uint32 sizeInBytes) override;

	virtual void initializeWithinPool(
		IndexBufferPool* pool,
		uint64 offsetInPool,
		uint32 sizeInBytes) override;

	virtual void updateData(
		RenderCommandList* commandList,
		void* data,
		EPixelFormat format) override;

	uint32 getIndexCount() override { return indexCount; }
	VkIndexType getIndexType() const { return vkIndexType; }
	VkBuffer getVkBuffer() const { return vkBuffer; }

private:
	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vkBufferMemory = VK_NULL_HANDLE;
	VkDeviceSize vkBufferSize = 0;
	uint32 indexCount = 0;
	VkIndexType vkIndexType = VK_INDEX_TYPE_MAX_ENUM;
};

#endif // COMPILE_BACKEND_VULKAN
