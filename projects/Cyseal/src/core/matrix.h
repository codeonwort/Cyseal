#pragma once

#include "vec3.h"
#include <memory.h>

// #todo-matrix: SIMD

// #todo-matrix: https://docs.microsoft.com/en-US/windows/win32/direct3dhlsl/dx-graphics-hlsl-per-component-math
// HLSL matrix packing order is column-major, so I need to choose one among several options:
//   1. Use row-major in application side and transpose final matrices before uploading to the GPU.
//   2. Use row-major in application side and use row_major float4x4 in HLSL.
//   3. Use column-major in application side and, transpose every data and reverse every mul order.
// Currently I'm going with 2nd option which is the most lazy way.

// Row-major (same convention as DirectXMath's XMMatrix)
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

	inline void copy(float* data)
	{
		memcpy_s(m, sizeof(m), data, sizeof(m));
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
// #todo-matrix: Fast multiplication
inline Matrix operator*(const Matrix& A, const Matrix& B)
{
	Matrix C;
	for (int32 i = 0; i < 4; ++i)
	{
		for (int32 j = 0; j < 4; ++j)
		{
			C.m[i][j] = A.m[i][0] * B.m[0][j]
				+ A.m[i][1] * B.m[1][j]
				+ A.m[i][2] * B.m[2][j]
				+ A.m[i][3] * B.m[3][j];
		}
	}
	return C;
}

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

	inline const Matrix& getMatrix() const { updateMatrix(); return m; }
	inline Matrix& getMatrix() { updateMatrix(); return m; }
	const float* getMatrixData() const;

private:
	void updateMatrix() const;

	vec3 position;
	// vec3 rotation; // #todo-matrix: Rotator
	vec3 scale;

	mutable bool dirty;
	mutable Matrix m;

};
