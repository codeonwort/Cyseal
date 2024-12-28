#pragma once

#include "core/matrix.h"
#include "core/plane.h"
#include "core/aabb.h"
#include "geometry/transform.h"

// Should match with Frustum3D in common.hlsl
struct CameraFrustum
{
	// 0: top, 1: bottom, 2: left, 3: right, 4: near, 5: far
	Plane3D planes[6];

	bool intersectsAABB(const AABB& box) const
	{
		for (int i = 0; i < 6; ++i)
		{
			if (!checkPlane(box, i)) return false;
		}
		return true;
	}

private:
	bool checkPlane(const AABB& box, int planeIndex) const
	{
		const Plane3D& plane = planes[planeIndex];
		vec3 boxCenter = 0.5 * (box.maxBounds + box.minBounds);
		vec3 boxHalfSize = 0.5 * (box.maxBounds - box.minBounds);
		float r = dot(boxHalfSize, abs(plane.normal));
		float s = dot(boxCenter, plane.normal) - plane.distance;
		return -r <= s;
	}
};

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

	// Set individual property of view matrix.
	void move(const vec3& forwardRightUp);
	void moveForward(float distance);
	void moveRight(float distance);
	void moveUp(float distance);
	void rotateYaw(float angleDegree); // mouse left/right in first person view
	void rotatePitch(float angleDegree); // mouse up/down in first person view

	void setPosition(const vec3& newPosition);
	void setYaw(float newYaw);
	void setPitch(float newPitch);

	inline vec3 getPosition() const { return position; }

	CameraFrustum getFrustum() const;

	inline const Matrix& getViewMatrix() const { return view; }
	inline const Matrix& getViewInvMatrix() const { return viewInv; }
	inline const Matrix& getProjMatrix() const { updateProjection(); return projection; }
	inline const Matrix& getProjInvMatrix() const { updateProjection(); return projectionInv; }
	inline Matrix getViewProjMatrix() const { updateViewProjection(); return view * projection; }
	// Returns inverse of (viwe * proj), which is (projInv * viewInv).
	inline Matrix getViewProjInvMatrix() const { updateViewProjection(); return projectionInv * viewInv; }

private:
	void updateView() const;
	void updateProjection() const;
	void updateViewProjection() const;

	// Projection transform
	float fovY_radians;
	float aspectRatioWH;
	float zNear;
	float zFar;

	// View transform
	vec3 position;
	float rotationX = 0.0f; // pitch (degrees)
	float rotationY = 0.0f; // yaw (degrees)

	mutable bool bViewDirty = true;
	mutable bool bProjectionDirty = true;
	mutable Matrix view;
	mutable Matrix viewInv;
	mutable Matrix projection;
	mutable Matrix projectionInv;
	mutable Matrix viewProjection;
	mutable Matrix viewProjectionInv;
};
