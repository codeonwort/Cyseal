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
	const float fovY = Cymath::radians(fovY_degrees);
	const float Y = Cymath::cot(fovY * 0.5f);
	const float X = Y / aspectWH;

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

	bDirty = true;
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
#endif
	view.copyFrom(V);

	bDirty = true;
}

void Camera::updateViewProjection() const
{
	if (bDirty)
	{
		viewProjection = view * projection;
		bDirty = false;
	}
}
