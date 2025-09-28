#include "gpu_scene.h"
#include "static_mesh.h"
#include "material.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
#include "rhi/texture_manager.h"
#include "core/matrix.h"
#include "world/scene_proxy.h"
#include "world/gpu_resource_asset.h"
#include "util/logging.h"

DEFINE_LOG_CATEGORY_STATIC(LogGPUScene);

// Should match with GPUSceneItem in common.hlsl
struct GPUSceneItem
{
	Float4x4 localToWorld;
	Float4x4 prevLocalToWorld;

	vec3     localMinBounds;
	uint32   positionBufferOffset;

	vec3     localMaxBounds;
	uint32   nonPositionBufferOffset;

	uint32   indexBufferOffset;
	vec3     _pad0;
};

enum EGPUSceneCommandType : uint32
{
	Update = 0,
};
struct GPUSceneCommand
{
	uint32 commandType; // EGPUSceneCommandType
	uint32 sceneItemIndex;
	uint32 _pad0;
	uint32 _pad1;
	GPUSceneItem sceneItem;
};



void GPUScene::initialize()
{
	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	gpuSceneCommandBufferMaxElements.resize(swapchainCount);
	gpuSceneCommandBuffer.initialize(swapchainCount);
	gpuSceneCommandBufferSRV.initialize(swapchainCount);

	totalVolatileDescriptors.resize(swapchainCount, 0);
	volatileViewHeap.initialize(swapchainCount);

	materialConstantsMaxCounts.resize(swapchainCount, 0);
	materialSRVMaxCounts.resize(swapchainCount, 0);
	materialConstantsActualCounts.resize(swapchainCount, 0);
	materialSRVActualCounts.resize(swapchainCount, 0);
	materialSRVHeap.initialize(swapchainCount);
	materialSRVs.initialize(swapchainCount);

	materialConstantsMemory.initialize(swapchainCount);
	materialConstantsHeap.initialize(swapchainCount);
	materialConstantsSRV.initialize(swapchainCount);

	// Shader
	ShaderStage* gpuSceneShader = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneCS");
	gpuSceneShader->declarePushConstants({ { "pushConstants", 1} });
	gpuSceneShader->loadFromFile(L"gpu_scene.hlsl", "mainCS");

	pipelineState = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
		ComputePipelineDesc{
			.cs = gpuSceneShader,
			.nodeMask = 0,
		}
	));

	delete gpuSceneShader; // No use after PSO creation.
}

void GPUScene::renderGPUScene(RenderCommandList* commandList, uint32 swapchainIndex, const GPUSceneInput& passInput)
{
	auto scene                    = passInput.scene;
	auto camera                   = passInput.camera;
	auto sceneUniform             = passInput.sceneUniform;
	auto bRenderAnyRaytracingPass = passInput.bRenderAnyRaytracingPass;

	uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();
	uint32 numMeshSections = 0;
	uint32 numDirtyMeshSections = 0;

	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		StaticMeshProxy* sm = scene->staticMeshes[i];
		uint32 currentSections = (uint32)(sm->getSections().size());
		numMeshSections += currentSections;
		if (sm->isTransformDirty() || sm->isLodDirty())
		{
			numDirtyMeshSections += currentSections;
		}
	}

	if (numMeshSections == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}

	const bool bRebuildGPUScene = scene->bRebuildGPUScene;
	uint32 numGPUSceneCommands = bRebuildGPUScene ? numMeshSections : numDirtyMeshSections;
	if (numGPUSceneCommands > gpuSceneMaxElements)
	{
		resizeGPUSceneBuffer(commandList, numMeshSections);
	}
	if (numGPUSceneCommands > 0)
	{
		resizeGPUSceneCommandBuffer(swapchainIndex, numGPUSceneCommands);
	}
	// #todo-gpuscene: Don't assume material_max_count == mesh_section_total_count.
	resizeMaterialBuffers(swapchainIndex, numMeshSections, numMeshSections);

	uint32 requiredVolatiles = 0;
	requiredVolatiles += 1; // sceneUniform
	requiredVolatiles += 1; // gpuSceneBuffer
	requiredVolatiles += 1; // commandBuffer
	resizeVolatileHeaps(swapchainIndex, requiredVolatiles);

	// #todo-gpuscene: Don't upload unchanged materials. Also don't copy one by one.
	// Prepare bindless materials.
	uint32& currentConstantsCount = materialConstantsActualCounts[swapchainIndex];
	uint32& currentMaterialSRVCount = materialSRVActualCounts[swapchainIndex];
	currentConstantsCount = 0;
	currentMaterialSRVCount = 0;
	{
		char eventString[128];
		sprintf_s(eventString, "UpdateMaterialBuffer (count=%u)", numMeshSections);
		SCOPED_DRAW_EVENT_STRING(commandList, eventString);

		auto& SRVs = materialSRVs[swapchainIndex];
		DescriptorHeap* srvHeap = materialSRVHeap.at(swapchainIndex);

		// #todo-gpuscene: Can't do this in resizeMaterialBuffers()
		// #todo-gpuscene: Destructors are slow here, when we can just wipe out them.
		SRVs.clear();
		SRVs.reserve(numMeshSections);
		srvHeap->resetAllDescriptors(); // Need to clear SRVs first.

		auto albedoFallbackTexture = gTextureManager->getSystemTextureGrey2D()->getGPUResource();

		std::vector<MaterialConstants> materialConstantsData(materialConstantsMaxCounts[swapchainIndex]);

		for (uint32 i = 0; i < numStaticMeshes; ++i)
		{
			StaticMeshProxy* staticMesh = scene->staticMeshes[i];
			for (const StaticMeshSection& section : staticMesh->getSections())
			{
				MaterialAsset* const material = section.material.get();

				// Texture SRV
				auto albedo = albedoFallbackTexture;
				if (material != nullptr && material->albedoTexture != nullptr)
				{
					albedo = material->albedoTexture->getGPUResource();
				}

				ShaderResourceViewDesc srvDesc{
					.format              = albedo->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = albedo->getCreateParams().mipLevels,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					}
				};
				auto albedoSRV = gRenderDevice->createSRV(albedo.get(), srvHeap, srvDesc);
				SRVs.emplace_back(UniquePtr<ShaderResourceView>(albedoSRV));

				// Constants
				MaterialConstants constants;
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
				constants.albedoTextureIndex = currentMaterialSRVCount;

				materialConstantsData[currentConstantsCount] = std::move(constants);

				// #todo-gpuscene: Currently always increment even if duplicate items are generated.
				++currentMaterialSRVCount;
				++currentConstantsCount;
			}
		}

		uint32 materialDataSize = (uint32)(sizeof(MaterialConstants) * materialConstantsData.size());
		materialConstantsMemory[swapchainIndex]->singleWriteToGPU(commandList, materialConstantsData.data(), materialDataSize, 0);
	}
	
	// #todo-gpuscene: Avoid recreation of buffers when bRebuildGPUScene == true.
	// There are various cases:
	// [ ] A new object is added to the scene.
	// [ ] An object is removed from the scene.
	// [v] No addition or removal but some objects changed their transforms.
	std::vector<GPUSceneCommand> sceneCommands(numGPUSceneCommands);
	uint32 sceneItemIx = 0;
	uint32 sceneCommandIx = 0;
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		StaticMeshProxy* sm = scene->staticMeshes[i];
		const uint32 smSections = (uint32)(sm->getSections().size());

		if (bRebuildGPUScene == false && sm->isTransformDirty() == false && sm->isLodDirty() == false)
		{
			sceneItemIx += smSections;
			continue;
		}

		const Float4x4 localToWorld = sm->getLocalToWorld();
		const Float4x4 prevLocalToWorld = sm->getPrevLocalToWorld();
		
		for (uint32 j = 0; j < smSections; ++j)
		{
			const StaticMeshSection& section = sm->getSections()[j];
			sceneCommands[sceneCommandIx].commandType                       = (uint32)EGPUSceneCommandType::Update;
			sceneCommands[sceneCommandIx].sceneItemIndex                    = sceneItemIx;
			sceneCommands[sceneCommandIx].sceneItem.localToWorld            = localToWorld;
			sceneCommands[sceneCommandIx].sceneItem.prevLocalToWorld        = prevLocalToWorld;
			sceneCommands[sceneCommandIx].sceneItem.localMinBounds          = section.localBounds.minBounds;
			// #todo-gpuscene: uint64 offset
			sceneCommands[sceneCommandIx].sceneItem.positionBufferOffset    = (uint32)section.positionBuffer->getGPUResource()->getBufferOffsetInBytes();
			sceneCommands[sceneCommandIx].sceneItem.localMaxBounds          = section.localBounds.maxBounds;
			sceneCommands[sceneCommandIx].sceneItem.nonPositionBufferOffset = (uint32)section.nonPositionBuffer->getGPUResource()->getBufferOffsetInBytes();
			sceneCommands[sceneCommandIx].sceneItem.indexBufferOffset       = (uint32)section.indexBuffer->getGPUResource()->getBufferOffsetInBytes();
			++sceneCommandIx;
			++sceneItemIx;
		}
	}

	const uint32 numSceneCommands = (uint32)sceneCommands.size();
	if (numSceneCommands > 0)
	{
		char eventString[128];
		if (bRebuildGPUScene)
		{
			sprintf_s(eventString, "RebuildSceneBuffer (count=%u)", numSceneCommands);
		}
		else
		{
			sprintf_s(eventString, "UpdateSceneBuffer (%u of %u)", numSceneCommands, numMeshSections);
		}
		SCOPED_DRAW_EVENT_STRING(commandList, eventString);

		Buffer::UploadBarrier uploadBarrier{
			.syncBefore   = EBarrierSync::ALL,
			.syncAfter    = EBarrierSync::COMPUTE_SHADING,
			.accessBefore = EBarrierAccess::COMMON,
			.accessAfter  = EBarrierAccess::SHADER_RESOURCE,
		};
		gpuSceneCommandBuffer[swapchainIndex]->singleWriteToGPU(
			commandList,
			sceneCommands.data(),
			(uint32)(sizeof(GPUSceneCommand) * numSceneCommands),
			0,
			uploadBarrier);

		BufferBarrier barriersBefore[] = {
			// No need for gpuSceneCommandBuffer due to custom uploadBarrier.
			//{ EBarrierSync::ALL, EBarrierSync::COMPUTE_SHADING, EBarrierAccess::COMMON, EBarrierAccess::SHADER_RESOURCE, gpuSceneCommandBuffer.at(swapchainIndex) },
			{ EBarrierSync::ALL, EBarrierSync::COMPUTE_SHADING, EBarrierAccess::COMMON, EBarrierAccess::UNORDERED_ACCESS, gpuSceneBuffer.get() },
		};
		commandList->barrier(_countof(barriersBefore), barriersBefore, 0, nullptr, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.pushConstant("pushConstants", numSceneCommands);
		SPT.rwStructuredBuffer("gpuSceneBuffer", gpuSceneBufferUAV.get());
		SPT.structuredBuffer("commandBuffer", gpuSceneCommandBufferSRV.at(swapchainIndex));

		commandList->setComputePipelineState(pipelineState.get());
		commandList->bindComputeShaderParameters(pipelineState.get(), &SPT, volatileViewHeap.at(swapchainIndex));
		commandList->dispatchCompute(numSceneCommands, 1, 1);

		BufferBarrier barriersAfter[] = {
			{ EBarrierSync::COMPUTE_SHADING, EBarrierSync::PIXEL_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::SHADER_RESOURCE, gpuSceneBuffer.get() },
		};
		commandList->barrier(_countof(barriersAfter), barriersAfter, 0, nullptr, 0, nullptr);
	}
}

ShaderResourceView* GPUScene::getGPUSceneBufferSRV() const
{
	return gpuSceneBufferSRV.get();
}

GPUScene::MaterialDescriptorsDesc GPUScene::queryMaterialDescriptors(uint32 swapchainIndex) const
{
	return MaterialDescriptorsDesc{
		.constantsBufferSRV = materialConstantsSRV.at(swapchainIndex),
		.srvHeap = materialSRVHeap.at(swapchainIndex),
		.srvCount = materialSRVActualCounts[swapchainIndex],
	};
}

void GPUScene::resizeVolatileHeaps(uint32 swapchainIndex, uint32 maxDescriptors)
{
	if (volatileViewHeap[swapchainIndex] == nullptr || totalVolatileDescriptors[swapchainIndex] < maxDescriptors)
	{
		totalVolatileDescriptors[swapchainIndex] = maxDescriptors;

		volatileViewHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV_SRV_UAV,
				.numDescriptors = maxDescriptors,
				.flags          = EDescriptorHeapFlags::ShaderVisible,
				.nodeMask       = 0,
			}
		));

		wchar_t debugName[256];
		swprintf_s(debugName, L"GPUScene_VolatileViewHeap_%u", swapchainIndex);
		volatileViewHeap[swapchainIndex]->setDebugName(debugName);

		CYLOG(LogGPUScene, Log, L"Resize volatile heap [%u]: %u descriptors", swapchainIndex, maxDescriptors);
	}
}

void GPUScene::resizeGPUSceneCommandBuffer(uint32 swapchainIndex, uint32 maxElements)
{
	if (gpuSceneCommandBuffer[swapchainIndex] == nullptr || gpuSceneCommandBufferMaxElements[swapchainIndex] < maxElements)
	{
		gpuSceneCommandBufferMaxElements[swapchainIndex] = maxElements;
		const uint32 viewStride = sizeof(GPUSceneCommand);

		gpuSceneCommandBuffer[swapchainIndex] = UniquePtr<Buffer>(gRenderDevice->createBuffer(
			BufferCreateParams{
				.sizeInBytes = viewStride * maxElements,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC
			}
		));
		wchar_t debugName[256];
		swprintf_s(debugName, L"Buffer_GPUSceneCommand_%u", swapchainIndex);
		gpuSceneCommandBuffer[swapchainIndex]->setDebugName(debugName);

		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::UNKNOWN;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = maxElements;
		srvDesc.buffer.structureByteStride = viewStride;
		srvDesc.buffer.flags               = EBufferSRVFlags::None;
		gpuSceneCommandBufferSRV[swapchainIndex] = UniquePtr<ShaderResourceView>(
			gRenderDevice->createSRV(gpuSceneCommandBuffer.at(swapchainIndex), srvDesc));
	}
}

void GPUScene::resizeGPUSceneBuffer(RenderCommandList* commandList, uint32 maxElements)
{
	gpuSceneMaxElements = maxElements;
	const uint32 viewStride = sizeof(GPUSceneItem);

	if (commandList != nullptr && gpuSceneBuffer != nullptr)
	{
		auto oldBuffer = gpuSceneBuffer.release();
		auto oldSRV = gpuSceneBufferSRV.release();
		auto oldUAV = gpuSceneBufferUAV.release();
		oldBuffer->setDebugName(L"Buffer_GPUScene_MarkedForDeath");
		commandList->enqueueDeferredDealloc(oldBuffer);
		commandList->enqueueDeferredDealloc(oldSRV);
		commandList->enqueueDeferredDealloc(oldUAV);
	}

	gpuSceneBuffer = UniquePtr<Buffer>(gRenderDevice->createBuffer(
		BufferCreateParams{
			.sizeInBytes = viewStride * gpuSceneMaxElements,
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::UAV,
		}
	));
	gpuSceneBuffer->setDebugName(L"Buffer_GPUScene");

	uint64 bufferSize = gpuSceneBuffer->getCreateParams().sizeInBytes;
	CYLOG(LogGPUScene, Log, L"Resize GPUScene buffer: %llu bytes (%.3f MiB)",
		bufferSize, (double)bufferSize / (1024.0f * 1024.0f));
	
	{
		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::UNKNOWN;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = gpuSceneMaxElements;
		srvDesc.buffer.structureByteStride = viewStride;
		srvDesc.buffer.flags               = EBufferSRVFlags::None;
		gpuSceneBufferSRV = UniquePtr<ShaderResourceView>(
			gRenderDevice->createSRV(gpuSceneBuffer.get(), srvDesc));
	}
	{
		UnorderedAccessViewDesc uavDesc{};
		uavDesc.format                      = EPixelFormat::UNKNOWN;
		uavDesc.viewDimension               = EUAVDimension::Buffer;
		uavDesc.buffer.firstElement         = 0;
		uavDesc.buffer.numElements          = gpuSceneMaxElements;
		uavDesc.buffer.structureByteStride  = viewStride;
		uavDesc.buffer.counterOffsetInBytes = 0;
		uavDesc.buffer.flags                = EBufferUAVFlags::None;
		gpuSceneBufferUAV = UniquePtr<UnorderedAccessView>(
			gRenderDevice->createUAV(gpuSceneBuffer.get(), uavDesc));
	}
}

// Bindless materials
void GPUScene::resizeMaterialBuffers(uint32 swapchainIndex, uint32 maxConstantsCount, uint32 maxSRVCount)
{
	auto align = [](uint32 size, uint32 alignment) -> uint32
	{
		return (size + (alignment - 1)) & ~(alignment - 1);
	};

	if (materialConstantsMaxCounts[swapchainIndex] < maxConstantsCount)
	{
		materialConstantsMaxCounts[swapchainIndex] = maxConstantsCount;

		// Was ConstantBuffer but now StructuredBuffer, so don't need memory alignment.
		const uint32 materialMemorySize = sizeof(MaterialConstants) * maxConstantsCount;

		CYLOG(LogGPUScene, Log, L"Resize material constants memory [%u]: %u bytes (%.3f MiB)",
			swapchainIndex, materialMemorySize, (float)materialMemorySize / (1024.0f * 1024.0f));

		materialConstantsMemory[swapchainIndex] = UniquePtr<Buffer>(gRenderDevice->createBuffer(
			BufferCreateParams{
				.sizeInBytes = materialMemorySize,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC,
			}
		));
		materialConstantsHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::SRV,
				.numDescriptors = maxConstantsCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
			}
		));
		materialConstantsSRV[swapchainIndex] = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(
			materialConstantsMemory.at(swapchainIndex),
			materialConstantsHeap.at(swapchainIndex),
			ShaderResourceViewDesc{
				.format                  = EPixelFormat::UNKNOWN,
				.viewDimension           = ESRVDimension::Buffer,
				.buffer                  = BufferSRVDesc{
					.firstElement        = 0,
					.numElements         = maxConstantsCount,
					.structureByteStride = sizeof(MaterialConstants),
					.flags               = EBufferSRVFlags::None,
				}
			}
		));
	}

	if (materialSRVMaxCounts[swapchainIndex] < maxSRVCount)
	{
		materialSRVMaxCounts[swapchainIndex] = maxSRVCount;

		materialSRVHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::SRV,
				.numDescriptors = maxSRVCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
			}
		));
	}
}
