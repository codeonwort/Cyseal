#pragma once

#include "assertion.h"

template<typename T>
struct Clamped
{
private:
	T x;
	T xMin;
	T xMax;

public:
	Clamped(T defaultValue, T minValue, T maxValue)
		: xMin(minValue), xMax(maxValue)
	{
		CHECK(minValue <= maxValue);
		operator=(defaultValue);
	}

	Clamped(const Clamped& other)
	{
		operator=(other.x);
	}

	Clamped& operator=(const Clamped& other)
	{
		operator=(other.x);
		return *this;
	}

	Clamped& operator=(T newValue)
	{
		x = (newValue < xMin) ? xMin : (newValue > xMax) ? xMax : newValue;
		return *this;
	}

	inline T getValue() const { return x; }
	inline T getMinValue() const { return xMin; }
	inline T getMaxValue() const { return xMax; }

	operator T() const { return x; }
};
