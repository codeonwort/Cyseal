#pragma once

#if COMPILE_BACKEND_VULKAN

#include "rhi/gpu_resource.h"
#include <vulkan/vulkan_core.h>

class RenderDevice;

// Generic wrapper of GPU buffer.
class VulkanBuffer : public Buffer
{
public:
	virtual ~VulkanBuffer();
	virtual void initialize(const BufferCreateParams& inCreateParams) override;
	virtual void writeToGPU(RenderCommandList* commandList, uint32 numUploads, Buffer::UploadDesc* uploadDescs) override;

	virtual void* getRawResource() const { return vkBuffer; }
	virtual void setDebugName(const wchar_t* inDebugName) override;

private:
	VkDeviceMemory vkBufferMemory = VK_NULL_HANDLE;
	VkBuffer vkBuffer = VK_NULL_HANDLE;
};

// Specialized wrapper for vertex buffer.
class VulkanVertexBuffer : public VertexBuffer
{
public:
	~VulkanVertexBuffer();

	//~ BEGIN GPUResource interface
	virtual void* getRawResource() const override { return getVkBuffer(); }
	//~ END GPUResource interface

	//~ BEGIN VertexBuffer interface
	virtual void initialize(uint32 sizeInBytes) override;
	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;
	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) override;
	virtual uint32 getVertexCount() const override { return vertexCount; };
	virtual uint64 getBufferOffsetInBytes() const override { return offsetInParentBuffer; }
	//~ END VertexBuffer interface

	VkBuffer getVkBuffer() const;

private:
	// internalBuffer is created only if current VulkanVertexBuffer is an independent buffer.
	// If current VulkanVertexBuffer was suballocated from a VertexBufferPool, internalBuffer is null.
	VulkanBuffer* internalBuffer = nullptr;

	uint32 vertexCount = 0;
	uint64 bufferSize = 0;
	uint64 offsetInParentBuffer = 0;
};

// Specialized wrapper for index buffer.
class VulkanIndexBuffer : public IndexBuffer
{
public:
	//~ BEGIN GPUResource interface
	virtual void* getRawResource() const override { return getVkBuffer(); }
	//~ END GPUResource interface

	//~ BEGIN IndexBuffer interface
	virtual void initialize(uint32 sizeInBytes, EPixelFormat format) override;
	virtual void initializeWithinPool(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;
	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) override;
	virtual uint32 getIndexCount() const override { return indexCount; }
	virtual EPixelFormat getIndexFormat() const override { return indexFormat; }
	virtual uint64 getBufferOffsetInBytes() const override { return offsetInParentBuffer; }
	//~ END IndexBuffer interface

	VkBuffer getVkBuffer() const;
	inline VkIndexType getVkIndexType() const { return vkIndexType; }

private:
	// internalBuffer is created only if current VulkanIndexBuffer is an independent buffer.
	// If current VulkanIndexBuffer was suballocated from an IndexBufferPool, internalBuffer is null.
	VulkanBuffer* internalBuffer = nullptr;

	EPixelFormat indexFormat = EPixelFormat::R32_UINT;
	uint32 indexCount = 0;
	uint64 offsetInParentBuffer = 0;

	VkDeviceSize vkBufferSize = 0;
	VkIndexType vkIndexType = VK_INDEX_TYPE_MAX_ENUM;
};

#endif // COMPILE_BACKEND_VULKAN
