#include "camera.h"
#include "core/cymath.h"
#include "rhi/rhi_policy.h"

#include <algorithm>

#define RIGHT_HANDED 1

static const float MAX_PITCH = 80.0f;
static const float MIN_PITCH = -80.0f;

static const vec3 forward0(0.0f, 0.0f, 1.0f);
static const vec3 right0(1.0f, 0.0f, 0.0f);
static const vec3 up0(0.0f, 1.0f, 0.0f);

Camera::Camera()
{
	perspective(90.0f, 1920.0f / 1080.0f, 1.0f, 1000.0f);
	lookAt(vec3(0.0f, 0.0f, 0.0f), forward0, up0);
}

void Camera::perspective(float fovY_degrees, float aspectWH, float n, float f)
{
	fovY_radians = Cymath::radians(fovY_degrees);
	aspectRatioWH = aspectWH;
	zNear = n;
	zFar = f;
	bProjectionDirty = true;
}

void Camera::setFovYInDegrees(float fovY_degrees)
{
	fovY_radians = Cymath::radians(fovY_degrees);
	bProjectionDirty = true;
}

void Camera::setAspectRatio(float width, float height)
{
	aspectRatioWH = width / height;
	bProjectionDirty = true;
}

void Camera::setAspectRatio(float inAspectRatioWH)
{
	aspectRatioWH = inAspectRatioWH;
	bProjectionDirty = true;
}

void Camera::setZNear(float inZNear)
{
	zNear = inZNear;
	bProjectionDirty = true;
}

void Camera::setZFar(float inZFar)
{
	zFar = inZFar;
	bProjectionDirty = true;
}

void Camera::lookAt(const vec3& origin, const vec3& target, const vec3& up)
{
#if RIGHT_HANDED
	vec3 Z = normalize(target - origin); // forward
	vec3 X = normalize(cross(Z, up));    // right
	vec3 Y = cross(X, Z);                // up
	float V[16] = {
		X.x,             Y.x,            -Z.x,             0.0f,
		X.y,             Y.y,            -Z.y,             0.0f,
		X.z,             Y.z,            -Z.z,             0.0f,
		-dot(X, origin), -dot(Y, origin), dot(Z, origin),  1.0f
	};
#else
	vec3 Z = normalize(target - origin); // forward
	vec3 X = normalize(cross(up, Z));    // right
	vec3 Y = cross(Z, X);                // up
	float V[16] = {
		X.x,             Y.x,            -Z.x,             0.0f,
		X.y,             Y.y,            -Z.y,             0.0f,
		X.z,             Y.z,            -Z.z,             0.0f,
		-dot(X, origin), -dot(Y, origin), -dot(Z, origin), 1.0f
	};
	float V_inv[16] = {
		X.x,             X.y,             X.z,             0.0f,
		Y.x,             Y.y,             Y.z,             0.0f,
		-Z.x,           -Z.y,            -Z.z,             0.0f,
		dot(X, origin),  dot(Y, origin), dot(Z, origin),   1.0f
	};
#endif
	Matrix L;
	L.copyFrom(V);
	L = L.transpose();
	vec3 v = L.transformDirection(forward0);
	v.normalize();

	position = origin;
	rotationX = Cymath::degrees(Cymath::asin(v.y));
	rotationY = Cymath::degrees((v.z >= 0.0f) ? -Cymath::asin(v.x) : Cymath::PI + Cymath::asin(v.x));

	bViewDirty = true;
}

void Camera::move(const vec3& forwardRightUp)
{
	updateView();
	vec3 delta = forwardRightUp.x * viewInv.transformDirection(-forward0);
	delta += forwardRightUp.y * viewInv.transformDirection(right0);
	delta += forwardRightUp.z * viewInv.transformDirection(up0);
	position += delta;
	bViewDirty = true;
}

void Camera::moveForward(float distance)
{
	updateView();
	position += distance * viewInv.transformDirection(-forward0);
	bViewDirty = true;
}

void Camera::moveRight(float distance)
{
	updateView();
	position += distance * viewInv.transformDirection(right0);
	bViewDirty = true;
}

void Camera::moveUp(float distance)
{
	updateView();
	position += distance * viewInv.transformDirection(up0);
	bViewDirty = true;
}

void Camera::rotateYaw(float angleDegree)
{
	rotationY -= angleDegree;
	bViewDirty = true;
}

void Camera::rotatePitch(float angleDegree)
{
	rotationX = std::clamp(rotationX + angleDegree, MIN_PITCH, MAX_PITCH);
	bViewDirty = true;
}

void Camera::setPosition(const vec3& newPosition)
{
	position = newPosition;
	bViewDirty = true;
}

void Camera::setYaw(float newYaw)
{
	rotationY = newYaw;
	bViewDirty = true;
}

void Camera::setPitch(float newPitch)
{
	rotationX = std::clamp(newPitch, MIN_PITCH, MAX_PITCH);
	bViewDirty = true;
}

CameraFrustum Camera::getFrustum() const
{
	const float hh_near = zNear * tanf(fovY_radians * 0.5f);
	const float hw_near = hh_near * aspectRatioWH;
	const float hh_far = zFar * tanf(fovY_radians * 0.5f);
	const float hw_far = hh_far * aspectRatioWH;

	vec3 vs[8];
	vs[0] = (-forward0 * zNear) + (right0 * hw_near) + (up0 * hh_near);
	vs[1] = (-forward0 * zNear) - (right0 * hw_near) + (up0 * hh_near);
	vs[2] = (-forward0 * zNear) + (right0 * hw_near) - (up0 * hh_near);
	vs[3] = (-forward0 * zNear) - (right0 * hw_near) - (up0 * hh_near);
	vs[4] = (-forward0 * zFar)  + (right0 * hw_far)  + (up0 * hh_far);
	vs[5] = (-forward0 * zFar)  - (right0 * hw_far)  + (up0 * hh_far);
	vs[6] = (-forward0 * zFar)  + (right0 * hw_far)  - (up0 * hh_far);
	vs[7] = (-forward0 * zFar)  - (right0 * hw_far)  - (up0 * hh_far);
	const auto& M = viewInv.m;
	for (uint32 i = 0; i < 8; ++i)
	{
		float x = dot(vs[i], vec3(M[0][0], M[1][0], M[2][0]));
		float y = dot(vs[i], vec3(M[0][1], M[1][1], M[2][1]));
		float z = dot(vs[i], vec3(M[0][2], M[1][2], M[2][2]));
		vs[i] = position + vec3(x, y, z);
	}

	CameraFrustum frustum;
	frustum.planes[0] = Plane3D::fromThreePoints(vs[0], vs[1], vs[4]);
	frustum.planes[1] = Plane3D::fromThreePoints(vs[2], vs[6], vs[3]);
	frustum.planes[2] = Plane3D::fromThreePoints(vs[1], vs[3], vs[5]);
	frustum.planes[3] = Plane3D::fromThreePoints(vs[0], vs[4], vs[2]);
	frustum.planes[4] = Plane3D::fromThreePoints(vs[2], vs[3], vs[0]);
	frustum.planes[5] = Plane3D::fromThreePoints(vs[6], vs[4], vs[7]);
	return frustum;
}

void Camera::updateView() const
{
	if (!bViewDirty) return;

	Transform R;
	R.setRotation(right0, rotationX);
	R.appendRotation(up0, rotationY);

	Matrix T;
	T.m[3][0] = -position.x;
	T.m[3][1] = -position.y;
	T.m[3][2] = -position.z;

	view = R.getMatrix();
	view = T * view;

	viewInv = view.inverse();

	bViewDirty = false;
}

void Camera::updateProjection() const
{
	if (!bProjectionDirty) return;

	const float Y = Cymath::cot(fovY_radians * 0.5f);
	const float X = Y / aspectRatioWH;
	const float n = zNear;
	const float f = zFar;

	// NDC z range = [0, 1]
#if RIGHT_HANDED
	if (getReverseZPolicy() == EReverseZPolicy::Reverse)
	{
		float P[16] = {
			X,       0.0f,    0.0f,               0.0f,
			0.0f,    Y,       0.0f,               0.0f,
			0.0f,    0.0f,    n / (f - n),       -1.0f,
			0.0f,    0.0f,    (n * f) / (f - n),  0.0f
		};
		projection.copyFrom(P);
	}
	else
	{
		float P[16] = {
			X,       0.0f,    0.0f,               0.0f,
			0.0f,    Y,       0.0f,               0.0f,
			0.0f,    0.0f,    f / (n - f),       -1.0f,
			0.0f,    0.0f,    -(n * f) / (f - n), 0.0f
		};
		projection.copyFrom(P);
	}
#else
	if (getReverseZPolicy() == EReverseZPolicy::Reverse)
	{
		float P[16] = {
			X,       0.0f,    0.0f,               0.0f,
			0.0f,    Y,       0.0f,               0.0f,
			0.0f,    0.0f,    -n / (f - n),       1.0f,
			0.0f,    0.0f,    (n * f) / (f - n),  0.0f
		};
		projection.copyFrom(P);
	}
	else
	{
		float P[16] = {
			X,       0.0f,    0.0f,               0.0f,
			0.0f,    Y,       0.0f,               0.0f,
			0.0f,    0.0f,    f / (f - n),        1.0f,
			0.0f,    0.0f,    -(n * f) / (f - n), 0.0f
		};
		projection.copyFrom(P);
	}
#endif
	// #todo-matrix: Derive the inverse manually
	// (generalized inverse math is slower and it loses precision)
	projectionInv = projection.inverse();

	bProjectionDirty = false;
}

void Camera::updateViewProjection() const
{
	updateView();
	updateProjection();
}
