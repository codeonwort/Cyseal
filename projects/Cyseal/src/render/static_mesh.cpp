#include "static_mesh.h"
#include "rhi/gpu_resource.h"
#include "world/scene.h"
#include "world/scene_proxy.h"
#include "memory/custom_new_delete.h"
#include "memory/mem_alloc.h"

static GPUSceneItem createGPUSceneItem(const StaticMeshSection& section, const Matrix& localToWorld, const Matrix& prevLocalToWorld)
{
	return GPUSceneItem{
		.localToWorld            = localToWorld,
		.prevLocalToWorld        = prevLocalToWorld,
		.localMinBounds          = section.localBounds.minBounds,
		.positionBufferOffset    = (uint32)section.positionBuffer->getGPUResource()->getBufferOffsetInBytes(), // #todo-gpuscene: uint64 offset
		.localMaxBounds          = section.localBounds.maxBounds,
		.nonPositionBufferOffset = (uint32)section.nonPositionBuffer->getGPUResource()->getBufferOffsetInBytes(),
		.indexBufferOffset       = (uint32)section.indexBuffer->getGPUResource()->getBufferOffsetInBytes(),
		.flags                   = GPUSceneItem::FlagBits::IsValid,
	};
}

static MaterialConstants createMaterialConstants(MaterialAsset* material, uint32 gpuSceneItemIx)
{
	MaterialConstants constants{};
	if (material != nullptr)
	{
		constants.albedoMultiplier  = material->albedoMultiplier;
		constants.roughness         = material->roughness;
		constants.emission          = material->emission;
		constants.metalMask         = material->metalMask;
		constants.materialID        = (uint32)material->materialID;
		constants.indexOfRefraction = material->indexOfRefraction;
		constants.transmittance     = material->transmittance;
	}
	// Filled by GPUScene when processing gpu scene commands.
	constants.albedoTextureIndex    = 0xffffffff;

	return constants;
}

static Texture* getAlbedoTexture(const SharedPtr<MaterialAsset>& material)
{
	if (material != nullptr && material->albedoTexture != nullptr)
	{
		return material->albedoTexture->getGPUResource().get();
	}
	return nullptr;
}

void StaticMesh::updateGPUSceneResidency(SceneProxy* sceneProxy, GPUSceneItemIndexAllocator* gpuSceneItemIndexAllocator)
{
	// NOTE: activeLOD should have been updated already.

	const std::vector<StaticMeshSection>& sections = LODs[activeLOD].sections;
	const size_t numSections = sections.size();

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
					const StaticMeshSection& section = sections[i];
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
					const StaticMeshSection& section = sections[i];
					const uint32 itemIx = gpuSceneItemIndexAllocator->allocate();
					gpuSceneResidency.itemIndices[i] = itemIx;

					GPUSceneAllocCommand allocCmd{
						.sceneItemIndex = itemIx,
						.sceneItem      = createGPUSceneItem(section, transform.getMatrix(), prevModelMatrix),
					};
					sceneProxy->gpuSceneAllocCommands.emplace_back(allocCmd);

					GPUSceneMaterialCommand materialCmd{
						.sceneItemIndex = itemIx,
						.materialData   = createMaterialConstants(section.material.get(), itemIx),
					};
					sceneProxy->gpuSceneMaterialCommands.emplace_back(materialCmd);
					sceneProxy->gpuSceneAlbedoTextures.push_back(getAlbedoTexture(section.material));
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
				gpuSceneItemIndexAllocator->deallocate(itemIx);

				GPUSceneEvictCommand cmd{
					.sceneItemIndex = itemIx
				};
				sceneProxy->gpuSceneEvictCommands.emplace_back(cmd);

				GPUSceneEvictMaterialCommand materialCmd{
					.sceneItemIndex = itemIx
				};
				sceneProxy->gpuSceneEvictMaterialCommands.emplace_back(materialCmd);
			}
			gpuSceneResidency.phase = EGPUResidencyPhase::NotAllocated;
			gpuSceneResidency.itemIndices.clear();
			break;
		case EGPUResidencyPhase::NeedToReallocate:
			for (size_t i = 0; i < gpuSceneResidency.itemIndices.size(); ++i)
			{
				const uint32 itemIx = gpuSceneResidency.itemIndices[i];
				gpuSceneItemIndexAllocator->deallocate(itemIx);

				GPUSceneEvictCommand cmd{
					.sceneItemIndex = itemIx
				};
				sceneProxy->gpuSceneEvictCommands.emplace_back(cmd);

				GPUSceneEvictMaterialCommand materialCmd{
					.sceneItemIndex = itemIx
				};
				sceneProxy->gpuSceneEvictMaterialCommands.emplace_back(materialCmd);
			}
			gpuSceneResidency.itemIndices.resize(numSections);
			for (size_t i = 0; i < numSections; ++i)
			{
				const StaticMeshSection& section = sections[i];
				const uint32 itemIx = gpuSceneItemIndexAllocator->allocate();
				gpuSceneResidency.itemIndices[i] = itemIx;

				GPUSceneAllocCommand allocCmd{
					.sceneItemIndex = itemIx,
					.sceneItem      = createGPUSceneItem(section, transform.getMatrix(), prevModelMatrix),
				};
				sceneProxy->gpuSceneAllocCommands.emplace_back(allocCmd);

				GPUSceneMaterialCommand materialCmd{
					.sceneItemIndex = itemIx,
					.materialData   = createMaterialConstants(section.material.get(), itemIx)
				};
				sceneProxy->gpuSceneMaterialCommands.emplace_back(materialCmd);
				sceneProxy->gpuSceneAlbedoTextures.push_back(getAlbedoTexture(section.material));
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

StaticMeshProxy* StaticMesh::createStaticMeshProxy(StackAllocator* allocator) const
{
	StaticMeshProxy* proxy = static_cast<StaticMeshProxy*>(allocator->alloc(sizeof(StaticMeshProxy)));

	proxy->lod              = &(LODs[activeLOD]);
	proxy->localToWorld     = transform.getMatrix();
	proxy->prevLocalToWorld = prevModelMatrix;
	proxy->bTransformDirty  = isTransformDirty();
	proxy->bLodDirty        = bLodDirty;

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
