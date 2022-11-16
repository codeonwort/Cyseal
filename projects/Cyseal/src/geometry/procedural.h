#pragma once

#include "primitive.h"

namespace ProceduralGeometry
{
	enum class EPlaneNormal { X = 0, Y = 1, Z = 2 };

	// Default: unit XY plane
	void plane(
		Geometry& outGeometry,
		float sizeX = 1.0f, float sizeY = 1.0f,
		uint32 numCellsX = 1, uint32 numCellsY = 1,
		EPlaneNormal up = EPlaneNormal::Z);

	void crumpledPaper(
		Geometry& outGeometry,
		float sizeX, float sizeY,
		uint32 numCellsX, uint32 numCellsY,
		float peak,
		EPlaneNormal up = EPlaneNormal::Z);

	// Default: Axis-aligned unit cube
	void cube(
		Geometry& outGeometry,
		float sizeX = 1.0f, float sizeY = 1.0f, float sizeZ = 1.0f);

	void icosphere(
		Geometry& outGeometry,
		uint32 iterations);

	// Just a random geometry to make icosphere variants
	void spikeBall(
		Geometry& outGeometry,
		uint32 subdivisions, float phase, float peak);
};
