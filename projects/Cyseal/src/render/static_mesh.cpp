#include "static_mesh.h"
#include "rhi/gpu_resource.h"

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
