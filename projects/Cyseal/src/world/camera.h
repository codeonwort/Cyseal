#pragma once

#include "core/matrix.h"

class Camera
{

public:
	Camera();

	void perspective(float fovY_degrees, float aspectWH, float zNear, float zFar);

	void lookAt(const vec3& origin, const vec3& target, const vec3& up);

	inline const Matrix& getMatrix() const
	{
		updateViewProjection();
		return viewProjection;
	}
	inline const float* getMatrixData() const
	{
		updateViewProjection();
		return &viewProjection.m[0][0];
	}

private:
	void updateViewProjection() const;

	Transform view;
	Matrix projection;
	mutable Matrix viewProjection;

};
