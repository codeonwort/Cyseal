#pragma once

// PLY file loader
// https://en.wikipedia.org/wiki/PLY_(file_format)
// http://gamma.cs.unc.edu/POWERPLANT/papers/ply.pdf

#include "core/vec2.h"
#include "core/vec3.h"
#include "core/smart_pointer.h"
#include "world/material_asset.h"

#include <string>
#include <vector>

// If the input file contains some non-triangular faces, they are split into triangles.
class PLYMesh
{
public:
	inline void applyTransform(const Matrix& transform)
	{
		for (size_t i = 0; i < positionBuffer.size(); ++i)
		{
			positionBuffer[i] = transform.transformPosition(positionBuffer[i]);
		}
		for (size_t i = 0; i < normalBuffer.size(); ++i)
		{
			normalBuffer[i] = transform.transformDirection(normalBuffer[i]);
		}
	}

	uint32 getVertexCount() const { return (uint32)positionBuffer.size(); }
	uint32 getIndexCount() const { return (uint32)indexBuffer.size(); }

	SharedPtr<MaterialAsset> material;

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
