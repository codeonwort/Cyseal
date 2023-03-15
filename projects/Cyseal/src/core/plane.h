#pragma once

#include "vec3.h"

struct Plane3D
{
	vec3 normal;
	float distance;

	static Plane3D fromNormalAndDistance(const vec3& n, float d)
	{
		return Plane3D{ n, d };
	}
	static Plane3D fromPointAndNormal(const vec3& p, const vec3& n)
	{
		return Plane3D{ n, dot(p, n) };
	}
	static Plane3D fromThreePoints(const vec3& a, const vec3& b, const vec3& c)
	{
		return fromPointAndNormal(a, normalize(cross(b - a, c - a)));
	}
};
