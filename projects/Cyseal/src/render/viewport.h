#pragma once

#include "core/int_types.h"

// #todo: Move to pipeline_state.h
struct Viewport
{
	float topLeftX;
	float topLeftY;
	float width;
	float height;
	float minDepth;
	float maxDepth;
};

struct ScissorRect
{
	uint32 left;
	uint32 top;
	uint32 right;
	uint32 bottom;
};
