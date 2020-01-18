#pragma once

#include <math.h>

class Cymath
{

public:
	static constexpr float PI        = 3.14159265358979323846f;
	static constexpr float TO_RADIAN = PI / 180.0f;
	static constexpr float TO_DEGREE = 180.0f / PI;

	static inline float radians(float degree);
	static inline float degrees(float radian);

	static inline float sqrt(float x);

	static inline float cos(float x);
	static inline float sin(float x);
	static inline float tan(float x);

	static inline float sec(float x);
	static inline float csc(float x);
	static inline float cot(float x);

	static inline float acos(float x);
	static inline float asin(float x);
	static inline float atan(float x);

};

float Cymath::radians(float degree)
{
	return degree * TO_RADIAN;
}

float Cymath::degrees(float radian)
{
	return radian * TO_DEGREE;
}

float Cymath::sqrt(float x)
{
	return sqrtf(x);
}

float Cymath::cos(float x)
{
	return cosf(x);
}

float Cymath::sin(float x)
{
	return sinf(x);
}

float Cymath::tan(float x)
{
	return tanf(x);
}

float Cymath::sec(float x)
{
	return 1.0f / Cymath::cos(x);
}

float Cymath::csc(float x)
{
	return 1.0f / Cymath::sin(x);
}

float Cymath::cot(float x)
{
	return 1.0f / Cymath::tan(x);
}

float Cymath::acos(float x)
{
	return acosf(x);
}

float Cymath::asin(float x)
{
	return asinf(x);
}

float Cymath::atan(float x)
{
	return atanf(x);
}
