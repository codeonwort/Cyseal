#pragma once

#include "core/types.h"
#include <vector>

struct Geometry
{
	std::vector<vec3> positions;
	std::vector<vec3> normals;
	std::vector<uint32> indices;
};

class GeometryGenerator
{

public:
	static void icosphere(uint32 iterations, Geometry& outGeometry);

	// Just a random geometry to make variants
	static void spikeBall(uint32 subdivisions, float phase, float peak, Geometry& outGeometry);

};
