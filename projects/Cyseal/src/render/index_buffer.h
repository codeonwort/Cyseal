#pragma once

#include <stdint.h>

struct IndexBufferCreateParams
{
	uint32_t numIndices;
	uint32_t elementSize;
};

class IndexBuffer
{

public:
	virtual void initialize(class RenderDevice* renderDevice) = 0;

	virtual void updateData(void* data, uint32_t sizeInBytes) = 0;

};
