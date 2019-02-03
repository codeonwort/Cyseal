#pragma once

#include "core/int_types.h"
#include <math.h>

class vec3
{

public:
	float x;
	float y;
	float z;

public:
	vec3() : vec3(0.0f, 0.0f, 0.0f) {}
	vec3(float e0, float e1, float e2)
	{
		x = e0;
		y = e1;
		z = e2;
	}

	inline const vec3& operator+() const { return *this; }
	inline vec3 operator-() const { return vec3(-x, -y, -z); }

	inline vec3& operator+=(const vec3 &v2);
	inline vec3& operator-=(const vec3 &v2);
	inline vec3& operator*=(const vec3 &v2);
	inline vec3& operator/=(const vec3 &v2);
	inline vec3& operator+=(const float t);
	inline vec3& operator-=(const float t);
	inline vec3& operator*=(const float t);
	inline vec3& operator/=(const float t);

	inline float length() const { return sqrtf(x*x + y * y + z * z); }
	inline float lengthSquared() const { return (x*x + y * y + z * z); }
	inline void normalize();

};

inline void vec3::normalize()
{
	float k = 1.0f / length();
	x *= k;
	y *= k;
	z *= k;
}

inline vec3 operator+(const vec3& v1, const vec3& v2)
{
	return vec3(v1.x + v2.x, v1.y + v2.y, v1.z + v2.z);
}
inline vec3 operator-(const vec3& v1, const vec3& v2)
{
	return vec3(v1.x - v2.x, v1.y - v2.y, v1.z - v2.z);
}
inline vec3 operator*(const vec3& v1, const vec3& v2)
{
	return vec3(v1.x * v2.x, v1.y * v2.y, v1.z * v2.z);
}
inline vec3 operator/(const vec3& v1, const vec3& v2)
{
	return vec3(v1.x / v2.x, v1.y / v2.y, v1.z / v2.z);
}

inline vec3 operator+(const vec3& v1, float t)
{
	return vec3(v1.x + t, v1.y + t, v1.z + t);
}
inline vec3 operator+(float t, const vec3& v1)
{
	return vec3(v1.x + t, v1.y + t, v1.z + t);
}
inline vec3 operator-(const vec3& v1, float t)
{
	return vec3(v1.x - t, v1.y - t, v1.z - t);
}
inline vec3 operator-(float t, const vec3& v1)
{
	return vec3(v1.x - t, v1.y - t, v1.z - t);
}

inline vec3 operator*(const vec3& v1, float t)
{
	return vec3(v1.x * t, v1.y * t, v1.z * t);
}
inline vec3 operator/(const vec3& v1, float t)
{
	return vec3(v1.x / t, v1.y / t, v1.z / t);
}
inline vec3 operator*(float t, const vec3& v1)
{
	return vec3(v1.x * t, v1.y * t, v1.z * t);
}

inline vec3 normalize(const vec3& v)
{
	vec3 u = v;
	u.normalize();
	return u;
}

inline float dot(const vec3& v1, const vec3& v2)
{
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

inline vec3 cross(const vec3& v1, const vec3& v2)
{
	return vec3(
		v1.y * v2.z - v1.z * v2.y,
		-(v1.x * v2.z - v1.z * v2.x),
		v1.x * v2.y - v1.y * v2.x
	);
}

inline vec3 reflect(const vec3& v, const vec3& n)
{
	return v - 2.0f * dot(v, n) * n;
}

inline bool refract(const vec3& v, const vec3& n, float ni_over_nt, vec3& outRefracted)
{
	vec3 uv = normalize(v);
	float dt = dot(uv, n);
	float D = 1.0f - ni_over_nt * ni_over_nt * (1.0f - dt * dt);
	if (D > 0.0f)
	{
		outRefracted = ni_over_nt * (uv - n * dt) - n * sqrtf(D);
		return true;
	}
	return false;
}

inline vec3& vec3::operator+=(const vec3& v)
{
	x += v.x;
	y += v.y;
	z += v.z;
	return *this;
}
inline vec3& vec3::operator-=(const vec3& v)
{
	x -= v.x;
	y -= v.y;
	z -= v.z;
	return *this;
}
inline vec3& vec3::operator*=(const vec3& v)
{
	x *= v.x;
	y *= v.y;
	z *= v.z;
	return *this;
}
inline vec3& vec3::operator/=(const vec3& v)
{
	x /= v.x;
	y /= v.y;
	z /= v.z;
	return *this;
}
inline vec3& vec3::operator+=(const float t)
{
	x += t;
	y += t;
	z += t;
	return *this;
}
inline vec3& vec3::operator-=(const float t)
{
	x -= t;
	y -= t;
	z -= t;
	return *this;
}
inline vec3& vec3::operator*=(const float t)
{
	x *= t;
	y *= t;
	z *= t;
	return *this;
}
inline vec3& vec3::operator/=(const float t)
{
	float k = 1.0f / t;
	x *= k;
	y *= k;
	z *= k;
	return *this;
}
