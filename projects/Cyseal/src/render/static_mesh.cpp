#include "static_mesh.h"
#include "rhi/gpu_resource.h"
#include "memory/custom_new_delete.h"

void StaticMesh::updateGPUSceneResidency(FreeNumberList* gpuSceneItemIndexAllocator)
{
	// NOTE: activeLOD should have been updated already.

	const size_t numSections = LODs[activeLOD].sections.size();

	if (gpuSceneResidency.phase == EGPUResidencyPhase::Allocated)
	{
		if (bLodDirty)
		{
			gpuSceneResidency.phase = EGPUResidencyPhase::NeedToReallocate;
		}
		else if (isTransformDirty())
		{
			gpuSceneResidency.phase = EGPUResidencyPhase::NeedToUpdate;
		}
	}

	switch (gpuSceneResidency.phase)
	{
		case EGPUResidencyPhase::NotAllocated:
			gpuSceneResidency.itemIndices.resize(numSections);
			for (size_t i = 0; i < numSections; ++i)
			{
				const uint32 itemIx = gpuSceneItemIndexAllocator->allocate();
				gpuSceneResidency.itemIndices[i] = itemIx;
				// #wip: Generate gpu scene command for alloc
			}
			gpuSceneResidency.phase = EGPUResidencyPhase::Allocated;
			break;
		case EGPUResidencyPhase::Allocated:
			// Do nothing.
			break;
		case EGPUResidencyPhase::NeedToEvict:
			for (size_t i = 0; i < numSections; ++i)
			{
				const uint32 itemIx = gpuSceneResidency.itemIndices[i];
				gpuSceneItemIndexAllocator->deallocate(itemIx);
				// #wip: Generate gpu scene command for evict
			}
			gpuSceneResidency.phase = EGPUResidencyPhase::NotAllocated;
			break;
		case EGPUResidencyPhase::NeedToReallocate:
			for (size_t i = 0; i < gpuSceneResidency.itemIndices.size(); ++i)
			{
				const uint32 itemIx = gpuSceneResidency.itemIndices[i];
				gpuSceneItemIndexAllocator->deallocate(itemIx);
			}
			gpuSceneResidency.itemIndices.resize(numSections);
			for (size_t i = 0; i < numSections; ++i)
			{
				const uint32 itemIx = gpuSceneItemIndexAllocator->allocate();
				gpuSceneResidency.itemIndices[i] = itemIx;
				// #wip: Generate gpu scene command for realloc
			}
			gpuSceneResidency.phase = EGPUResidencyPhase::Allocated;
			break;
		case EGPUResidencyPhase::NeedToUpdate:
			for (size_t i = 0; i < numSections; ++i)
			{
				const Matrix& localToWorld = transform.getMatrix();
				const Matrix& prevLocalToWorld = prevModelMatrix;
				// #wip: Generate gpu scene command for update
			}
			gpuSceneResidency.phase = EGPUResidencyPhase::Allocated;
			break;
		default:
			CHECK_NO_ENTRY();
			break;
	}
}

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
