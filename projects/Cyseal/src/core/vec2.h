#pragma once

#include "core/int_types.h"
#include <math.h>

class vec2
{

public:
	float x;
	float y;

public:
	vec2() : vec2(0.0f, 0.0f) {}
	vec2(float e0, float e1)
	{
		x = e0;
		y = e1;
	}

	inline bool operator==(const vec2& v2) { return x == v2.x && y == v2.y; }
	inline bool operator!=(const vec2& v2) { return !(*this == v2); }

	inline const vec2& operator+() const { return *this; }
	inline vec2 operator-() const { return vec2(-x, -y); }

	inline vec2& operator+=(const vec2& v2);
	inline vec2& operator-=(const vec2& v2);
	inline vec2& operator*=(const vec2& v2);
	inline vec2& operator/=(const vec2& v2);
	inline vec2& operator+=(const float t);
	inline vec2& operator-=(const float t);
	inline vec2& operator*=(const float t);
	inline vec2& operator/=(const float t);

	inline float length() const { return sqrtf(x * x + y * y); }
	inline float lengthSquared() const { return (x * x + y * y); }
	inline void normalize();

};

inline void vec2::normalize()
{
	float k = 1.0f / length();
	x *= k;
	y *= k;
}

inline vec2 operator+(const vec2& v1, const vec2& v2)
{
	return vec2(v1.x + v2.x, v1.y + v2.y);
}
inline vec2 operator-(const vec2& v1, const vec2& v2)
{
	return vec2(v1.x - v2.x, v1.y - v2.y);
}
inline vec2 operator*(const vec2& v1, const vec2& v2)
{
	return vec2(v1.x * v2.x, v1.y * v2.y);
}
inline vec2 operator/(const vec2& v1, const vec2& v2)
{
	return vec2(v1.x / v2.x, v1.y / v2.y);
}

inline vec2 operator+(const vec2& v1, float t)
{
	return vec2(v1.x + t, v1.y + t);
}
inline vec2 operator+(float t, const vec2& v1)
{
	return vec2(v1.x + t, v1.y + t);
}
inline vec2 operator-(const vec2& v1, float t)
{
	return vec2(v1.x - t, v1.y - t);
}
inline vec2 operator-(float t, const vec2& v1)
{
	return vec2(v1.x - t, v1.y - t);
}

inline vec2 operator*(const vec2& v1, float t)
{
	return vec2(v1.x * t, v1.y * t);
}
inline vec2 operator/(const vec2& v1, float t)
{
	return vec2(v1.x / t, v1.y / t);
}
inline vec2 operator*(float t, const vec2& v1)
{
	return vec2(v1.x * t, v1.y * t);
}

inline vec2 normalize(const vec2& v)
{
	vec2 u = v;
	u.normalize();
	return u;
}

inline float dot(const vec2& v1, const vec2& v2)
{
	return v1.x * v2.x + v1.y * v2.y;
}

inline vec2 reflect(const vec2& v, const vec2& n)
{
	return v - 2.0f * dot(v, n) * n;
}

inline bool refract(const vec2& v, const vec2& n, float ni_over_nt, vec2& outRefracted)
{
	vec2 uv = normalize(v);
	float dt = dot(uv, n);
	float D = 1.0f - ni_over_nt * ni_over_nt * (1.0f - dt * dt);
	if (D > 0.0f)
	{
		outRefracted = ni_over_nt * (uv - n * dt) - n * sqrtf(D);
		return true;
	}
	return false;
}

inline vec2& vec2::operator+=(const vec2& v)
{
	x += v.x;
	y += v.y;
	return *this;
}
inline vec2& vec2::operator-=(const vec2& v)
{
	x -= v.x;
	y -= v.y;
	return *this;
}
inline vec2& vec2::operator*=(const vec2& v)
{
	x *= v.x;
	y *= v.y;
	return *this;
}
inline vec2& vec2::operator/=(const vec2& v)
{
	x /= v.x;
	y /= v.y;
	return *this;
}
inline vec2& vec2::operator+=(const float t)
{
	x += t;
	y += t;
	return *this;
}
inline vec2& vec2::operator-=(const float t)
{
	x -= t;
	y -= t;
	return *this;
}
inline vec2& vec2::operator*=(const float t)
{
	x *= t;
	y *= t;
	return *this;
}
inline vec2& vec2::operator/=(const float t)
{
	float k = 1.0f / t;
	x *= k;
	y *= k;
	return *this;
}
