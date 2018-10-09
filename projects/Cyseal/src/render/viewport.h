#pragma once

#include <stdint.h>

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
	uint32_t left;
	uint32_t top;
	uint32_t right;
	uint32_t bottom;
};
