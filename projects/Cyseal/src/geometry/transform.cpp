#include "transform.h"

void Transform::setPosition(const vec3& newPosition)
{
	position = newPosition;
	bDirty = true;
}

void Transform::setRotation(const vec3& axis, float angleInDegrees)
{
	float t = 0.5f * Cymath::radians(angleInDegrees);
	rotation = quaternion(axis * Cymath::sin(t), Cymath::cos(t));
	bDirty = true;
}

void Transform::appendRotation(const vec3& axis, float angleInDegrees)
{
	float t = 0.5f * Cymath::radians(angleInDegrees);
	quaternion Q(axis * Cymath::sin(t), Cymath::cos(t));
	rotation = Q * rotation;
	bDirty = true;
}

void Transform::setScale(float newScale)
{
	setScale(vec3(newScale, newScale, newScale));
}

void Transform::setScale(const vec3& newScale)
{
	scale = newScale;
	bDirty = true;
}

const float* Transform::getMatrixData() const
{
	updateMatrix();
	return &(m.m[0][0]);
}

void Transform::updateMatrix() const
{
	if (bDirty)
	{
		m.identity();
		m.m[0][0] = scale.x;
		m.m[1][1] = scale.y;
		m.m[2][2] = scale.z;

		m = rotation.toMatrix() * m;

		m.m[3][0] = position.x;
		m.m[3][1] = position.y;
		m.m[3][2] = position.z;

		bDirty = false;
	}
}
