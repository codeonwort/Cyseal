#pragma once

#include "core/core_minimal.h"
#include "core/smart_pointer.h"
#include "geometry/transform.h"
#include "render/material.h"
#include "world/gpu_resource_asset.h"
#include <vector>

struct StaticMeshSection
{
	SharedPtr<VertexBufferAsset> positionBuffer;
	SharedPtr<VertexBufferAsset> nonPositionBuffer;
	SharedPtr<IndexBufferAsset>  indexBuffer;
	SharedPtr<MaterialAsset>     material;
	AABB                         localBounds;
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
		SharedPtr<VertexBufferAsset> positionBuffer,
		SharedPtr<VertexBufferAsset> nonPositionBuffer,
		SharedPtr<IndexBufferAsset> indexBuffer,
		SharedPtr<MaterialAsset> material,
		const AABB& localBounds);

	inline const std::vector<StaticMeshSection>& getSections(uint32 lod) const
	{
		CHECK(lod < LODs.size());
		return LODs[lod].sections;
	}

	inline size_t getNumLODs() const { return LODs.size(); }
	inline uint32 getActiveLOD() const { return activeLOD; }
	inline void setActiveLOD(uint32 lod)
	{
		bLodDirty = bLodDirty || (activeLOD != lod);
		activeLOD = lod;
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

	inline bool isLodDirty() const { return bLodDirty; }

	inline void clearDirtyFlags() { bTransformDirty = bLodDirty = false; }

private:
	std::vector<StaticMeshLOD> LODs;
	uint32 activeLOD = 0;

	Transform transform;
	bool bTransformDirty = false;
	bool bLodDirty = false;
};
