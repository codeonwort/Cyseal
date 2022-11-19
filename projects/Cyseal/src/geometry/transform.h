#pragma once

#include "core/matrix.h"
#include "core/quaternion.h"

class Transform
{

public:
	Transform()
		: bDirty(false)
		, scale(vec3(1.0f, 1.0f, 1.0f))
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

	vec3 position;
	quaternion rotation;
	vec3 scale;

	mutable bool bDirty;
	mutable Matrix m;

};
