#include "camera.h"
#include "core/cymath.h"

#define RIGHT_HANDED 1

Camera::Camera()
{
	perspective(90.0f, 1920.0f / 1080.0f, 1.0f, 1000.0f);
	lookAt(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), vec3(0.0f, 1.0f, 0.0f));
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
	position = origin;

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
	view.copyFrom(V);
	viewInv = view.inverse();
}

void Camera::getFrustum(Plane3D outPlanes[6]) const
{
	const float hh_near = zNear * tanf(fovY_radians * 0.5f);
	const float hw_near = hh_near * aspectRatioWH;
	const float hh_far = zFar * tanf(fovY_radians * 0.5f);
	const float hw_far = hh_far * aspectRatioWH;

	const vec3 forward0(0.0f, 0.0f, 1.0f);
	const vec3 right0(1.0f, 0.0f, 0.0f);
	const vec3 up0(0.0f, 1.0f, 0.0f);

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

	outPlanes[0] = Plane3D::fromThreePoints(vs[0], vs[1], vs[4]);
	outPlanes[1] = Plane3D::fromThreePoints(vs[2], vs[6], vs[3]);
	outPlanes[2] = Plane3D::fromThreePoints(vs[1], vs[3], vs[5]);
	outPlanes[3] = Plane3D::fromThreePoints(vs[0], vs[4], vs[2]);
	outPlanes[4] = Plane3D::fromThreePoints(vs[2], vs[3], vs[0]);
	outPlanes[5] = Plane3D::fromThreePoints(vs[6], vs[4], vs[7]);
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
	float P[16] = {
		X,       0.0f,    0.0f,               0.0f,
		0.0f,    Y,       0.0f,               0.0f,
		0.0f,    0.0f,    f / (n - f),       -1.0f,
		0.0f,    0.0f,    -(n * f) / (f - n), 0.0f
	};
#else
	float P[16] = {
		X,       0.0f,    0.0f,               0.0f,
		0.0f,    Y,       0.0f,               0.0f,
		0.0f,    0.0f,    f / (f - n),        1.0f,
		0.0f,    0.0f,    -(n * f) / (f - n), 0.0f
	};
#endif
	projection.copyFrom(P);
	// #todo-matrix: Derive the inverse manually
	// (generalized inverse math is slower and it loses precision)
	projectionInv = projection.inverse();

	bProjectionDirty = false;
}

void Camera::updateViewProjection() const
{
	updateProjection();
}
