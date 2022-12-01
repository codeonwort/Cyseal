#pragma once

#include "core/matrix.h"
#include "core/quaternion.h"

// Traditional Scale-Rotation-Translation matrix
class Transform
{

public:
	Transform()
	{
	}

	inline vec3 getPosition() const { return position; }
	inline quaternion getRotation() const { return rotation; }
	inline vec3 getScale() const { return scale; }

	void setPosition(const vec3& newPosition);
	void setRotation(const vec3& axis, float angle);
	void setScale(float newScale);
	void setScale(const vec3& newScale);

	inline const Matrix& getMatrix() const { updateMatrix(); return m; }
	inline Matrix& getMatrix() { updateMatrix(); return m; }
	const float* getMatrixData() const;

private:
	void updateMatrix() const;

	vec3       position = vec3(0.0f, 0.0f, 0.0f);
	quaternion rotation;
	vec3       scale    = vec3(1.0f, 1.0f, 1.0f);

	mutable bool bDirty = false;
	mutable Matrix m;

};
