#pragma once

#include "core/types.h"
#include "core/aabb.h"
#include "core/smart_pointer.h"
#include "world/gpu_resource_asset.h"
#include <vector>

struct Geometry;
class StaticMesh;
class MaterialAsset;

struct MesoGeometry
{
	std::vector<uint32> indices;
	AABB localBounds;

public:
	inline uint32 getIndexBufferTotalBytes() const
	{
		return (uint32)(indices.size() * sizeof(uint32));
	}

	inline void* getIndexBlob() const
	{
		return (void*)indices.data();
	}

public:
	static bool needsToPartition(const Geometry* G, uint32 maxTriangleCount);

	/// <summary>
	/// Divide a Geometry into multiple MesoGeometry instances so that each one's triangle count does not exceed the threshold.
	/// They all share the same vertex buffer data. Only their index buffer data + local bounds differ.
	/// </summary>
	/// <param name="G">The geomety to divide.</param>
	/// <param name="maxTriangleCount">Max triangle count for each Geometry.</param>
	/// <returns>A vector of MesoGeometry. CAUTION: The caller must deallocate the vector manually.</returns>
	static std::vector<MesoGeometry>* partitionByTriangleCount(const Geometry* G, uint32 maxTriangleCount);
};

struct MesoGeometryAssets
{
	SharedPtr<VertexBufferAsset> positionBufferAsset;
	SharedPtr<VertexBufferAsset> nonPositionBufferAsset;

	std::vector<SharedPtr<IndexBufferAsset>> indexBufferAsset;
	std::vector<AABB> localBounds;

	inline size_t numMeso() const { return indexBufferAsset.size(); }

	static MesoGeometryAssets createFrom(const Geometry* G);

	static void addStaticMeshSections(StaticMesh* mesh, const MesoGeometryAssets& assets, SharedPtr<MaterialAsset> material);
};
