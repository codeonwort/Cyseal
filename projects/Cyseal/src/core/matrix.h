#pragma once

#include "vec3.h"
#include <memory.h>

// #todo: SIMD

// row-major
class Matrix
{
	friend class Transform;

public:
	float m[4][4];

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

	inline float trace() const
	{
		return m[0][0] + m[1][1] + m[2][2] + m[3][3];
	}

	inline Matrix& operator+=(const Matrix& other);
	inline Matrix& operator-=(const Matrix& other);
	inline Matrix& operator*=(const Matrix& other);

};

inline Matrix operator+(const Matrix& A, const Matrix& B)
{
	Matrix C = A;
	C += B;
	return C;
}
inline Matrix operator-(const Matrix& A, const Matrix& B)
{
	Matrix C = A;
	C -= B;
	return C;
}
inline Matrix operator*(const Matrix& A, const Matrix& B);

//////////////////////////////////////////////////////////////////////////

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

