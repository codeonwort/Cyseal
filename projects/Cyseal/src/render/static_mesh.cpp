#include "static_mesh.h"
#include "rhi/gpu_resource.h"
#include "world/scene_proxy.h"
#include "memory/custom_new_delete.h"

void StaticMesh::updateGPUSceneResidency(SceneProxy* sceneProxy, FreeNumberList* gpuSceneItemIndexAllocator)
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
			{
				// Check first if GPU resources are valid.
				bool bInvalidResources = false;
				for (size_t i = 0; i < numSections; ++i)
				{
					const StaticMeshSection& section = LODs[activeLOD].sections[i];
					if (section.positionBuffer->getGPUResource() == nullptr
						|| section.nonPositionBuffer->getGPUResource() == nullptr
						|| section.indexBuffer->getGPUResource() == nullptr)
					{
						bInvalidResources = true;
						break;
					}
				}
				if (bInvalidResources)
				{
					break;
				}
				gpuSceneResidency.itemIndices.resize(numSections);
				for (size_t i = 0; i < numSections; ++i)
				{
					const StaticMeshSection& section = LODs[activeLOD].sections[i];
					const uint32 itemIx = gpuSceneItemIndexAllocator->allocate() - 1;
					gpuSceneResidency.itemIndices[i] = itemIx;

					GPUSceneAllocCommand cmd{
						.sceneItemIndex = itemIx,
						.sceneItem      = GPUSceneItem{
							.localToWorld            = transform.getMatrix(),
							.prevLocalToWorld        = prevModelMatrix,
							.localMinBounds          = section.localBounds.minBounds,
							.positionBufferOffset    = (uint32)section.positionBuffer->getGPUResource()->getBufferOffsetInBytes(), // #todo-gpuscene: uint64 offset
							.localMaxBounds          = section.localBounds.maxBounds,
							.nonPositionBufferOffset = (uint32)section.nonPositionBuffer->getGPUResource()->getBufferOffsetInBytes(),
							.indexBufferOffset       = (uint32)section.indexBuffer->getGPUResource()->getBufferOffsetInBytes(),
							.flags                   = GPUSceneItem::FlagBits::IsValid,
						}
					};
					sceneProxy->gpuSceneAllocCommands.emplace_back(cmd);
				}
				gpuSceneResidency.phase = EGPUResidencyPhase::Allocated;
			}
			break;
		case EGPUResidencyPhase::Allocated:
			// Do nothing.
			break;
		case EGPUResidencyPhase::NeedToEvict:
			for (size_t i = 0; i < numSections; ++i)
			{
				const uint32 itemIx = gpuSceneResidency.itemIndices[i];
				gpuSceneItemIndexAllocator->deallocate(itemIx + 1);

				GPUSceneEvictCommand cmd{
					.sceneItemIndex = itemIx
				};
				sceneProxy->gpuSceneEvictCommands.emplace_back(cmd);
			}
			gpuSceneResidency.phase = EGPUResidencyPhase::NotAllocated;
			gpuSceneResidency.itemIndices.clear();
			break;
		case EGPUResidencyPhase::NeedToReallocate:
			for (size_t i = 0; i < gpuSceneResidency.itemIndices.size(); ++i)
			{
				const uint32 itemIx = gpuSceneResidency.itemIndices[i];
				gpuSceneItemIndexAllocator->deallocate(itemIx + 1);

				GPUSceneEvictCommand cmd{
					.sceneItemIndex = itemIx
				};
				sceneProxy->gpuSceneEvictCommands.emplace_back(cmd);
			}
			gpuSceneResidency.itemIndices.resize(numSections);
			for (size_t i = 0; i < numSections; ++i)
			{
				const StaticMeshSection& section = LODs[activeLOD].sections[i];
				const uint32 itemIx = gpuSceneItemIndexAllocator->allocate() - 1;
				gpuSceneResidency.itemIndices[i] = itemIx;

				GPUSceneAllocCommand cmd{
					.sceneItemIndex = itemIx,
					.sceneItem      = GPUSceneItem{
						.localToWorld            = transform.getMatrix(),
						.prevLocalToWorld        = prevModelMatrix,
						.localMinBounds          = section.localBounds.minBounds,
						.positionBufferOffset    = (uint32)section.positionBuffer->getGPUResource()->getBufferOffsetInBytes(), // #todo-gpuscene: uint64 offset
						.localMaxBounds          = section.localBounds.maxBounds,
						.nonPositionBufferOffset = (uint32)section.nonPositionBuffer->getGPUResource()->getBufferOffsetInBytes(),
						.indexBufferOffset       = (uint32)section.indexBuffer->getGPUResource()->getBufferOffsetInBytes(),
						.flags                   = GPUSceneItem::FlagBits::IsValid,
					}
				};
				sceneProxy->gpuSceneAllocCommands.emplace_back(cmd);
			}
			gpuSceneResidency.phase = EGPUResidencyPhase::Allocated;
			break;
		case EGPUResidencyPhase::NeedToUpdate:
			// #wip: What if geometry or material changes while the section count remains same?
			for (size_t i = 0; i < numSections; ++i)
			{
				const uint32 itemIx = gpuSceneResidency.itemIndices[i];

				GPUSceneUpdateCommand cmd{
					.sceneItemIndex   = itemIx,
					.localToWorld     = transform.getMatrix(),
					.prevLocalToWorld = prevModelMatrix,
				};
				sceneProxy->gpuSceneUpdateCommands.emplace_back(cmd);
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
