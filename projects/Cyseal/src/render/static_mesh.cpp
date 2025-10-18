#include "static_mesh.h"
#include "rhi/gpu_resource.h"
#include "memory/custom_new_delete.h"

StaticMeshProxy* StaticMesh::createStaticMeshProxy() const
{
	StaticMeshProxy* proxy = new(EMemoryTag::Renderer) StaticMeshProxy{
		.lod              = LODs[activeLOD],
		.localToWorld     = transform.getMatrix(),
		.prevLocalToWorld = prevModelMatrix,
		.bTransformDirty  = isTransformDirty(),
		.bLodDirty        = bLodDirty,
	};
	return proxy;
}

void StaticMesh::addSection(
	uint32 lod,
	SharedPtr<VertexBufferAsset> positionBuffer,
	SharedPtr<VertexBufferAsset> nonPositionBuffer,
	SharedPtr<IndexBufferAsset> indexBuffer,
	SharedPtr<MaterialAsset> material,
	const AABB& localBounds)
{
	if (LODs.size() <= lod)
	{
		LODs.resize(lod + 1);
	}
	LODs[lod].sections.emplace_back(
		StaticMeshSection{
			.positionBuffer    = positionBuffer,
			.nonPositionBuffer = nonPositionBuffer,
			.indexBuffer       = indexBuffer,
			.material          = material,
			.localBounds       = localBounds,
		}
	);
}

bool StaticMesh::isTransformDirty() const
{
	return (transformDirtyCounter > 0) || (prevModelMatrix != transform.getMatrix());
}
