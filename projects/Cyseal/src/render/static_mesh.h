#pragma once

#include "core/core_minimal.h"
#include "core/smart_pointer.h"
#include "geometry/transform.h"
#include "render/material.h"
#include "world/gpu_resource_asset.h"

#include <vector>

class FreeNumberList;

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

struct StaticMeshProxy
{
	StaticMeshLOD lod;
	Matrix        localToWorld;
	Matrix        prevLocalToWorld;
	bool          bTransformDirty;
	bool          bLodDirty;

	inline const std::vector<StaticMeshSection>& getSections() const { return lod.sections; }
	inline const Matrix& getLocalToWorld() const { return localToWorld; }
	inline const Matrix& getPrevLocalToWorld() const { return prevLocalToWorld; }
	inline bool isTransformDirty() const { return bTransformDirty; }
	inline bool isLodDirty() const { return bLodDirty; }
};

class StaticMesh
{
public:
	void updateGPUSceneResidency(FreeNumberList* gpuSceneItemIndexAllocator);
	StaticMeshProxy* createStaticMeshProxy() const;

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
		transformDirtyCounter = 2;
	}
	inline void setRotation(const vec3& axis, float angle)
	{
		transform.setRotation(axis, angle);
		transformDirtyCounter = 2;
	}
	inline void setScale(float newScale)
	{
		setScale(vec3(newScale, newScale, newScale));
	}
	inline void setScale(const vec3& newScale)
	{
		transform.setScale(newScale);
		transformDirtyCounter = 2;
	}

	inline const Matrix& getTransformMatrix() const { return transform.getMatrix(); }
	bool isTransformDirty() const;

	inline bool isLodDirty() const { return bLodDirty; }

	inline void savePrevTransform() { prevModelMatrix = transform.getMatrix(); }
	inline void clearDirtyFlags()
	{
		transformDirtyCounter -= 1;
		if (transformDirtyCounter < 0) transformDirtyCounter = 0;
		bLodDirty = false;
	}

private:
	std::vector<StaticMeshLOD> LODs;
	uint32 activeLOD = 0;

	Transform transform;
	Matrix prevModelMatrix;
	int32 transformDirtyCounter = 0; // Was a boolean, but modified to update prev model matrix.
	bool bLodDirty = false;

private:
	enum class EGPUResidencyPhase : uint32
	{
		NotAllocated     = 0,
		Allocated        = 1,
		NeedToEvict      = 2, // Allocated but need to evict.
		NeedToReallocate = 3, // Allocated but need to evict and allocate again. (e.g., LOD change)
		NeedToUpdate     = 4, // Allocated but need to update in-place. (e.g., transform change)
	};
	struct GPUSceneResidency
	{
		EGPUResidencyPhase phase = EGPUResidencyPhase::NotAllocated;
		// #wip: Could be just [start,end) if indices are consecutive.
		// FreeNumberList does not provide such API yet...
		std::vector<uint32> itemIndices;
	};
	GPUSceneResidency gpuSceneResidency;
};
