#pragma once

#include <stdint.h>
#include "gpu_resource.h"
#include "pixel_format.h"

//////////////////////////////////////////////////////////////////////////
// Vertex Buffer

struct VertexBufferCreateParams
{
	uint32_t numVertices;
	uint32_t elementSize;
};

class VertexBuffer : public GPUResource
{
	
public:
	virtual void initialize(void* initialData, uint32_t sizeInBytes, uint32_t strideInBytes) = 0;

	virtual void updateData(void* data, uint32_t sizeInBytes, uint32_t strideInBytes) = 0;

};

//////////////////////////////////////////////////////////////////////////
// Index Buffer

struct IndexBufferCreateParams
{
	uint32_t numIndices;
	uint32_t elementSize;
};

class IndexBuffer : public GPUResource
{

public:
	virtual void initialize(void* initialData, uint32_t sizeInBytes, EPixelFormat format) = 0;

	virtual void updateData(void* data, uint32_t sizeInBytes, EPixelFormat format) = 0;

	virtual uint32_t getIndexCount() = 0;

};
