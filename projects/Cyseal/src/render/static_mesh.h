#pragma once

#include "core/core_minimal.h"
#include "geometry/transform.h"
#include "render/material.h"
#include "world/gpu_resource_asset.h"
#include <vector>

struct StaticMeshSection
{
	std::shared_ptr<VertexBufferAsset> positionBuffer;
	std::shared_ptr<VertexBufferAsset> nonPositionBuffer;
	std::shared_ptr<IndexBufferAsset>  indexBuffer;
	std::shared_ptr<Material> material;
};

struct StaticMeshLOD
{
	std::vector<StaticMeshSection> sections;
};

class StaticMesh
{
public:
	void addSection(
		uint32 lod,
		std::shared_ptr<VertexBufferAsset> positionBuffer,
		std::shared_ptr<VertexBufferAsset> nonPositionBuffer,
		std::shared_ptr<IndexBufferAsset> indexBuffer,
		std::shared_ptr<Material> material);

	inline const std::vector<StaticMeshSection>& getSections(uint32 lod) const
	{
		CHECK(lod < LODs.size());
		return LODs[lod].sections;
	}

	inline vec3 getPosition() const { return transform.getPosition(); }
	inline quaternion getRotation() const { return transform.getRotation(); }
	inline vec3 getScale() const { return transform.getScale(); }

	inline void setPosition(const vec3& newPosition)
	{
		transform.setPosition(newPosition);
		bTransformDirty = true;
	}
	inline void setRotation(const vec3& axis, float angle)
	{
		transform.setRotation(axis, angle);
		bTransformDirty = true;
	}
	inline void setScale(float newScale)
	{
		setScale(vec3(newScale, newScale, newScale));
	}
	inline void setScale(const vec3& newScale)
	{
		transform.setScale(newScale);
		bTransformDirty = true;
	}

	inline Matrix getTransformMatrix() { return transform.getMatrix(); }
	inline const Matrix& getTransformMatrix() const { return transform.getMatrix(); }
	inline bool isTransformDirty() const { return bTransformDirty; }
	inline void clearDirtyFlags() { bTransformDirty = false; }

private:
	std::vector<StaticMeshLOD> LODs;

	Transform transform;
	bool bTransformDirty = false;
};
