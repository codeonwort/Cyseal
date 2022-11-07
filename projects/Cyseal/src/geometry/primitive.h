#pragma once

#include "core/types.h"
#include <vector>

struct Geometry
{
	std::vector<vec3> positions;
	std::vector<vec3> normals;
	std::vector<uint32> indices;

	inline uint32 getPositionStride() const
	{
		return (uint32)(sizeof(vec3));
	}

	inline uint32 getPositionBufferTotalBytes() const
	{
		return (uint32)(positions.size() * sizeof(vec3));
	}

	inline uint32 getIndexBufferTotalBytes() const
	{
		return (uint32)(indices.size() * sizeof(uint32));
	}

};
