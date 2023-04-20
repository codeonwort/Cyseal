#pragma once

// PLY file loader
// https://en.wikipedia.org/wiki/PLY_(file_format)
// http://gamma.cs.unc.edu/POWERPLANT/papers/ply.pdf

#include <string>
#include <vector>
#include "core/vec2.h"
#include "core/vec3.h"

// If the input file contains some non-triangular faces, they are split into triangles.
class PLYMesh
{
public:
	uint32 getVertexCount() const { return (uint32)positionBuffer.size(); }
	uint32 getIndexCount() const { return (uint32)indexBuffer.size(); }

	std::vector<vec3> positionBuffer;
	std::vector<vec3> normalBuffer;
	std::vector<vec2> texcoordBuffer;
	std::vector<uint32> indexBuffer;
};

class PLYLoader
{
public:
	// @return Parsed PLY mesh. Should dealloc yourself. Null if load has failed.
	PLYMesh* loadFromFile(const std::wstring& filepath);
};
