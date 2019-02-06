#pragma once

#include "core/matrix.h"

class Camera
{

public:
	Camera()
	{
	}

	void setPerspective(float fovY_degrees, float aspectWH, float zNear, float zFar);

	void lookAt(const vec3& origin, const vec3& target, const vec3& up);

	inline const Matrix& getMatrix() const { return transform.getMatrix(); }
	inline const float* getMatrixData() const { return transform.getMatrixData(); }

private:
	Transform transform;
	Matrix projection;

};
