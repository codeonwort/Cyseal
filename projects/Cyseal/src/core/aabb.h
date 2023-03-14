#pragma once

#include "core/vec3.h"

struct AABB
{
	vec3 minBounds;
	vec3 maxBounds;

	static AABB fromMinMax(const vec3& minV, const vec3& maxV)
	{
		return AABB{ minV, maxV };
	}
	static AABB fromCenterAndHalfSize(const vec3& center, const vec3& halfSize)
	{
		return AABB{ center - halfSize, center + halfSize };
	}

	inline vec3 getCenter() const { return 0.5f * (minBounds + maxBounds); }
	inline vec3 getHalfSize() const { return 0.5f * (maxBounds - minBounds); }
	inline vec3 getSize() const { return (maxBounds - minBounds); }
};

inline AABB operator+(const AABB& a, const AABB& b)
{
	vec3 minBounds = vecMin(a.minBounds, b.minBounds);
	vec3 maxBounds = vecMax(a.minBounds, b.maxBounds);
	return AABB::fromMinMax(minBounds, maxBounds);
}
