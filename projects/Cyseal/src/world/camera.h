#pragma once

#include "core/matrix.h"
#include "core/plane.h"

class Camera
{
public:
	Camera();

	// Set properties of projection matrix at once.
	void perspective(float fovY_degrees, float aspectWH, float zNear, float zFar);

	// Set individual property of projection matrix.
	void setFovYInDegrees(float fovY_degrees);
	void setAspectRatio(float width, float height);
	void setAspectRatio(float aspectRatioWH);
	void setZNear(float zNear);
	void setZFar(float zFar);

	inline float getAspectRatio() const { return aspectRatioWH; }

	// Set properties of view matrix at once.
	void lookAt(const vec3& origin, const vec3& target, const vec3& up);

	void getFrustum(Plane3D outPlanes[6]) const;

	inline vec3 getPosition() const { return position; }

	inline const Matrix& getViewMatrix() const { return view; }
	inline const Matrix& getViewInvMatrix() const { return viewInv; }
	inline const Matrix& getProjMatrix() const { updateProjection(); return projection; }
	inline const Matrix& getProjInvMatrix() const { updateProjection(); return projectionInv; }
	inline Matrix getViewProjMatrix() const { updateViewProjection(); return view * projection; }
	// Returns inverse of (viwe * proj), which is (projInv * viewInv).
	inline Matrix getViewProjInvMatrix() const { updateViewProjection(); return projectionInv * viewInv; }

private:
	void updateProjection() const;
	void updateViewProjection() const;

	float fovY_radians;
	float aspectRatioWH;
	float zNear;
	float zFar;

	vec3 position;

	mutable bool bProjectionDirty = true;
	mutable Matrix view;
	mutable Matrix viewInv;
	mutable Matrix projection;
	mutable Matrix projectionInv;
	mutable Matrix viewProjection;
	mutable Matrix viewProjectionInv;
};
