#include "matrix.h"

// #todo-matrix: Abuse DirectXMath for inverse matrix
#include <DirectXMath.h>

Matrix& Matrix::operator+=(const Matrix& other)
{
	for (int32 i = 0; i < 16; ++i)
	{
		*(&m[0][0] + i) += *(&other.m[0][0] + i);
	}
	return *this;
}

Matrix& Matrix::operator-=(const Matrix& other)
{
	for (int32 i = 0; i < 16; ++i)
	{
		*(&m[0][0] + i) -= *(&other.m[0][0] + i);
	}
	return *this;
}

Matrix& Matrix::operator*=(const Matrix& other)
{
	*this = (*this) * other;
	return *this;
}

Matrix Matrix::inverse()
{
	float* mPtr = &(this->m[0][0]);
	DirectX::XMMATRIX xm_src(mPtr);
	DirectX::XMMATRIX xm_inv = DirectX::XMMatrixInverse(nullptr, xm_src);
	DirectX::XMFLOAT4X4 float4x4_inv;
	DirectX::XMStoreFloat4x4(&float4x4_inv, xm_inv);
	
	Matrix inv;
	inv.copyFrom(&(float4x4_inv.m[0][0]));
	return inv;
}
