#pragma once

#include "primitive.h"

namespace ProceduralGeometry
{
	void icosphere(uint32 iterations, Geometry& outGeometry);

	// Just a random geometry to make icosphere variants
	void spikeBall(uint32 subdivisions, float phase, float peak, Geometry& outGeometry);
};
