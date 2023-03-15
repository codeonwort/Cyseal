#include "static_mesh.h"
#include "rhi/gpu_resource.h"

void StaticMesh::addSection(
	uint32 lod,
	std::shared_ptr<VertexBufferAsset> positionBuffer,
	std::shared_ptr<VertexBufferAsset> nonPositionBuffer,
	std::shared_ptr<IndexBufferAsset> indexBuffer,
	std::shared_ptr<Material> material,
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
