#pragma once

#include <stdint.h>
#include "pixel_format.h"

class RenderDevice;

enum EVertexInputClassification
{
	PER_VERTEX,
	PER_INSTANCE
};

struct VertexInputElement
{
	const char* semantic;
	uint32_t semanticIndex;
	EPixelFormat format;
	uint32_t inputSlot;
	uint32_t alignedByteOffset;
	EVertexInputClassification inputSlotClass;
	uint32_t instanceDataStepRate;
};

class VertexInputLayout
{
	
public:
	virtual void initialize(RenderDevice* renderDevice) = 0;

protected:
	RenderDevice* device;

};
