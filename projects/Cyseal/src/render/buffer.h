#pragma once

#include "gpu_resource.h"
#include "pixel_format.h"

//////////////////////////////////////////////////////////////////////////
// Vertex Buffer

struct VertexBufferCreateParams
{
	uint32 numVertices;
	uint32 elementSize;
};

class VertexBuffer : public GPUResource
{
	
public:
	virtual void initialize(void* initialData, uint32 sizeInBytes, uint32 strideInBytes) = 0;

	virtual void updateData(void* data, uint32 sizeInBytes, uint32 strideInBytes) = 0;

};

//////////////////////////////////////////////////////////////////////////
// Index Buffer

struct IndexBufferCreateParams
{
	uint32 numIndices;
	uint32 elementSize;
};

class IndexBuffer : public GPUResource
{

public:
	virtual void initialize(void* initialData, uint32 sizeInBytes, EPixelFormat format) = 0;

	virtual void updateData(void* data, uint32 sizeInBytes, EPixelFormat format) = 0;

	virtual uint32 getIndexCount() = 0;

};
