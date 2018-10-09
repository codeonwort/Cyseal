#pragma once

#include <stdint.h>

struct VertexBufferCreateParams
{
	uint32_t numVertices;
	uint32_t elementSize;
};

class VertexBuffer
{
	
public:
	virtual void initialize(class RenderDevice* renderDevice) = 0;

	virtual void updateData(void* data, uint32_t sizeInBytes) = 0;

};
