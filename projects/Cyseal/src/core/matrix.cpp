#include "matrix.h"

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
