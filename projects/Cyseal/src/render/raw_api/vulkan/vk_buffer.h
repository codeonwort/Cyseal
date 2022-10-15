#pragma once

#include "render/gpu_resource.h"

// #todo-vulkan: VertexBuffer
class VulkanVertexBuffer : public VertexBuffer
{
public:
	virtual void initialize(uint32 sizeInBytes) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

};

// #todo-vulkan: IndexBuffer
class VulkanIndexBuffer : public IndexBuffer
{
public:
	void initialize(uint32 sizeInBytes) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void initializeWithinPool(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	uint32 getIndexCount() override
	{
		//throw std::logic_error("The method or operation is not implemented.");
		return 0;
	}
};
