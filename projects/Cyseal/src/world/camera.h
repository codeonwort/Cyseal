#pragma once

#include "core/matrix.h"

class Camera
{
public:
	Camera();

	void perspective(float fovY_degrees, float aspectWH, float zNear, float zFar);

	void lookAt(const vec3& origin, const vec3& target, const vec3& up);

	inline vec3 getPosition() const { return position; }

	inline const Matrix& getViewMatrix() const
	{
		return view;
	}

	inline const Matrix& getProjMatrix() const
	{
		return projection;
	}

	inline const Matrix& getViewProjMatrix() const
	{
		updateViewProjection();
		return viewProjection;
	}

private:
	void updateViewProjection() const;

	vec3 position;

	Matrix view;
	Matrix projection;

	mutable Matrix viewProjection;
	mutable bool bDirty = true;
};
