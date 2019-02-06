#pragma once

#include "vec3.h"
#include <memory.h>

class Matrix
{
	friend class Transform;

public:
	Matrix()
	{
		identity();
	}

	void identity()
	{
		static float I[4][4] = { {1,0,0,0}, {0,1,0,0},{0,0,1,0},{0,0,0,1} };
		memcpy_s(m, sizeof(m), I, sizeof(I));
	}

	// #todo: operators (+, -, *)

private:
	float m[4][4];

};

class Transform
{

public:
	Transform()
		: dirty(false)
	{
	}

	void setPosition(const vec3& newPosition);
	void setScale(float newScale);
	void setScale(const vec3& newScale);

	inline const Matrix& getMatrix() const { return m; }
	const float* getMatrixData() const;

private:
	void updateMatrix() const;

	vec3 position;
	// vec3 rotation;
	vec3 scale;

	mutable bool dirty;
	mutable Matrix m;

};
