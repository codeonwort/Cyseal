#include "camera.h"
#include "core/cymath.h"

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

	float P[16] = {
		X,       0.0f,    0.0f,               0.0f,
		0.0f,    Y,       0.0f,               0.0f,
		0.0f,    0.0f,    f / (f - n),        1.0f,
		0.0f,    0.0f,    -n * f / (f - n),   0.0f
	};
	projection.copyFrom(P);

	bDirty = true;
}

void Camera::lookAt(const vec3& origin, const vec3& target, const vec3& up)
{
	vec3 Z = normalize(target - origin); // forward
	vec3 X = normalize(cross(up, Z));    // right
	vec3 Y = cross(Z, X);                // up

	//float V[16] = {
	//	X.x,             Y.x,             Z.x,             0.0f,
	//	X.y,             Y.y,             Z.y,             0.0f,
	//	X.z,             Y.z,             Z.z,             0.0f,
	//	-dot(X, target), -dot(Y, target), -dot(Z, target), 1.0f
	//};
	float V[16] = {
		X.x,      X.y,      X.z,         0.0f,
		Y.x,      Y.y,      Y.z,         0.0f,
		Z.x,      Z.y,      Z.z,         0.0f,
		-origin.x, -origin.y, -origin.z, 1.0f
	};
	view.getMatrix().copyFrom(V);

	bDirty = true;
}

void Camera::updateViewProjection() const
{
	if (bDirty)
	{
		viewProjection = view.getMatrix() * projection;
		bDirty = false;
	}
}
