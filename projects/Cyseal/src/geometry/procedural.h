#pragma once

#include "primitive.h"

namespace ProceduralGeometry
{
	enum class EPlaneNormal { X = 0, Y = 1, Z = 2 };

	void plane(
		Geometry& outGeometry,
		float sizeX, float sizeY,
		uint32 numCellsX = 1, uint32 numCellsY = 1,
		EPlaneNormal up = EPlaneNormal::Z);

	void icosphere(uint32 iterations, Geometry& outGeometry);

	// Just a random geometry to make icosphere variants
	void spikeBall(uint32 subdivisions, float phase, float peak, Geometry& outGeometry);
};
