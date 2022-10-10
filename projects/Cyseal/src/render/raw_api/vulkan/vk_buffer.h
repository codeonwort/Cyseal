#pragma once

#include "render/gpu_resource.h"

// #todo-vulkan: VertexBuffer
class VulkanVertexBuffer : public VertexBuffer
{
public:
	void initialize(void* initialData, uint32 sizeInBytes, uint32 strideInBytes) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void updateData(void* data, uint32 sizeInBytes, uint32 strideInBytes) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

};

// #todo-vulkan: IndexBuffer
class VulkanIndexBuffer : public IndexBuffer
{
public:
	void initialize(void* initialData, uint32 sizeInBytes, EPixelFormat format) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void updateData(void* data, uint32 sizeInBytes, EPixelFormat format) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	uint32 getIndexCount() override
	{
		//throw std::logic_error("The method or operation is not implemented.");
		return 0;
	}

};