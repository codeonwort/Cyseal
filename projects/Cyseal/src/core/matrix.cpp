#include "matrix.h"

Matrix& Matrix::operator+=(const Matrix& other)
{
	for (int32 i = 0; i < 16; ++i) *(&m[0][0] + i) += *(&other.m[0][0] + i);
	return *this;
}

Matrix& Matrix::operator-=(const Matrix& other)
{
	for (int32 i = 0; i < 16; ++i) *(&m[0][0] + i) -= *(&other.m[0][0] + i);
	return *this;
}

Matrix& Matrix::operator*=(const Matrix& other)
{
	*this = (*this) * other;
	return *this;
}

void Transform::setPosition(const vec3& newPosition)
{
	position = newPosition;
	dirty = true;
}

void Transform::setScale(float newScale)
{
	setScale(vec3(newScale, newScale, newScale));
}

void Transform::setScale(const vec3& newScale)
{
	scale = newScale;
	dirty = true;
}

const float* Transform::getMatrixData() const
{
	if(dirty)
	{
		updateMatrix();
	}
	return &(m.m[0][0]);
}

void Transform::updateMatrix() const
{
	m.identity();
	m.m[0][0] = scale.x;
	m.m[1][1] = scale.y;
	m.m[2][2] = scale.z;
	m.m[0][3] = position.x;
	m.m[1][3] = position.y;
	m.m[2][3] = position.z;
}
