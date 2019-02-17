#pragma once

#include "cymath.h"
#include "vec3.h"
#include "matrix.h"

// #todo: SIMD

class quaternion
{

public:
	float x;
	float y;
	float z;
	float w;

	quaternion()
		: x(0.0f), y(0.0f), z(0.0f), w(1.0f)
	{
	}

	quaternion(float x0, float y0, float z0, float w0)
		: x(x0), y(y0), z(z0), w(w0)
	{
	}

	quaternion(const vec3& v, float w0)
		: x(v.x), y(v.y), z(v.z), w(w0)
	{
	}

	inline void identity()
	{
		x = y = z = 0.0f;
		w = 1.0f;
	}

	// A unit quaternion satisfies norm() == 1
	inline float norm() const
	{
		return Cymath::sqrt(x*x + y * y + z * z + w * w);
	}

	inline quaternion conjugate() const
	{
		return quaternion(-x, -y, -z, w);
	}

	inline quaternion inverse() const
	{
		float denom = norm();
		denom *= denom;

		return denom * conjugate();
	}

	Matrix toMatrix() const;

	inline quaternion& operator+=(const quaternion& other);
	inline quaternion& operator*=(const quaternion& other);

};

Matrix quaternion::toMatrix() const
{
	float s = 2.0f / norm();

	Matrix M;
	M.m[0][0] = 1.0f - s * (y * y + z * z);
	M.m[0][1] = s * (x * y - w * z);
	M.m[0][2] = s * (x * z + w * y);
	M.m[0][3] = 0.0f;
	M.m[1][0] = s * (x * y + w * z);
	M.m[1][1] = 1.0f - s * (x * x + z * z);
	M.m[1][2] = s * (y * z - w * x);
	M.m[1][3] = 0.0f;
	M.m[2][0] = s * (x * z - w * y);
	M.m[2][1] = s * (y * z + w * x);
	M.m[2][2] = 1.0f - s * (x * x + y * y);
	M.m[2][3] = 0.0f;
	M.m[3][0] = 0.0f;
	M.m[3][1] = 0.0f;
	M.m[3][2] = 0.0f;
	M.m[3][3] = 1.0f;

	return M;
}

quaternion& quaternion::operator+=(const quaternion& other)
{
	x += other.x;
	y += other.y;
	z += other.z;
	w += other.w;
	return *this;
}

quaternion& quaternion::operator*=(const quaternion& other)
{
	vec3 v(x, y, z);
	vec3 r(other.x, other.y, other.z);

	vec3 i = cross(v, r) + other.w * v + w * r;
	x = i.x;
	y = i.y;
	z = i.z;
	w = w * other.w - dot(v, r);

	return *this;
}

inline quaternion operator*(float t, const quaternion& q)
{
	return quaternion(q.x * t, q.y * t, q.z * t, q.w * t);
}
inline quaternion operator*(const quaternion& q, float t)
{
	return quaternion(q.x * t, q.y * t, q.z * t, q.w * t);
}
inline quaternion operator*(const quaternion& q1, const quaternion& q2)
{
	quaternion r = q1;
	r *= q2;
	return r;
}
