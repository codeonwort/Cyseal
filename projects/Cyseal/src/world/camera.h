#pragma once

#include "core/matrix.h"
#include "core/plane.h"

class Camera
{
public:
	Camera();

	void perspective(float fovY_degrees, float aspectWH, float zNear, float zFar);

	void lookAt(const vec3& origin, const vec3& target, const vec3& up);

	void getFrustum(Plane3D outPlanes[6]) const;

	inline vec3 getPosition() const { return position; }

	inline const Matrix& getViewMatrix() const { return view; }
	inline const Matrix& getProjMatrix() const { return projection; }
	inline const Matrix& getViewInvMatrix() const { return viewInv; }
	inline const Matrix& getProjInvMatrix() const { return projectionInv; }

	inline const Matrix& getViewProjMatrix() const
	{
		updateViewProjection();
		return viewProjection;
	}
	inline const Matrix& getViewProjInvMatrix() const
	{
		updateViewProjection();
		return viewProjectionInv;
	}

private:
	void updateViewProjection() const;

	float fovY_radians;
	float aspectRatioWH;
	float zNear;
	float zFar;

	vec3 position;

	Matrix view;
	Matrix projection;

	Matrix viewInv;
	Matrix projectionInv;

	mutable Matrix viewProjection;
	mutable Matrix viewProjectionInv;
	mutable bool bDirty = true;
};
