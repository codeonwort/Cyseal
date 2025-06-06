#pragma once

#include "vec3.h"
#include <memory.h>

// #todo-matrix: SIMD

// NOTE: Do not use this as shader parameter. Use Float4x4.
// Row-major (same convention as DirectXMath's XMMatrix)
// https://docs.microsoft.com/en-US/windows/win32/direct3dhlsl/dx-graphics-hlsl-per-component-math
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

	inline void identity()
	{
		static float I[4][4] = { {1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1} };
		memcpy_s(m, sizeof(m), I, sizeof(I));
	}

	inline void scale(float x, float y, float z)
	{
		static float S[4][4] = { {x,0,0,0},{0,y,0,0},{0,0,z,0},{0,0,0,1} };
		memcpy_s(m, sizeof(m), S, sizeof(S));
	}

	Matrix inverse() const;

	Matrix transpose() const;

	inline void copyFrom(float* data)
	{
		memcpy_s(m, sizeof(m), data, sizeof(m));
	}

	inline float trace() const
	{
		return m[0][0] + m[1][1] + m[2][2] + m[3][3];
	}

	inline vec3 transformPosition(const vec3& v) const
	{
		float x = dot(v, vec3(m[0][0], m[1][0], m[2][0]));
		float y = dot(v, vec3(m[0][1], m[1][1], m[2][1]));
		float z = dot(v, vec3(m[0][2], m[1][2], m[2][2]));
		vec3 o = vec3(m[3][0], m[3][1], m[3][2]);
		return o + vec3(x, y, z);
	}
	inline vec3 transformDirection(const vec3& v) const
	{
		float x = dot(v, vec3(m[0][0], m[1][0], m[2][0]));
		float y = dot(v, vec3(m[0][1], m[1][1], m[2][1]));
		float z = dot(v, vec3(m[0][2], m[1][2], m[2][2]));
		return vec3(x, y, z);
	}

	inline bool operator==(const Matrix& other) const;
	inline bool operator!=(const Matrix& other) const;
	inline Matrix& operator+=(const Matrix& other);
	inline Matrix& operator-=(const Matrix& other);
	inline Matrix& operator*=(const Matrix& other);
};

bool Matrix::operator==(const Matrix& other) const
{
	auto p = &(m[0][0]);
	auto q = &(other.m[0][0]);
	for (uint32 i = 0; i < 16; ++i)
	{
		if (*p++ != *q++) return false;
	}
	return true;
}

bool Matrix::operator!=(const Matrix& other) const
{
	return !(*this == other);
}

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

// Use this for shader parameter.
struct Float4x4
{
	float m[4][4];

	Float4x4()
	{
		memset(m, 0, sizeof(m));
	}

	// Transpose of M
	Float4x4(const Matrix& M)
	{
		m[0][0] = M.m[0][0];
		m[0][1] = M.m[1][0];
		m[0][2] = M.m[2][0];
		m[0][3] = M.m[3][0];

		m[1][0] = M.m[0][1];
		m[1][1] = M.m[1][1];
		m[1][2] = M.m[2][1];
		m[1][3] = M.m[3][1];

		m[2][0] = M.m[0][2];
		m[2][1] = M.m[1][2];
		m[2][2] = M.m[2][2];
		m[2][3] = M.m[3][2];

		m[3][0] = M.m[0][3];
		m[3][1] = M.m[1][3];
		m[3][2] = M.m[2][3];
		m[3][3] = M.m[3][3];
	}
};
