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
#include "world/scene.h"
#include "world/gpu_resource_asset.h"
#include "util/logging.h"

DEFINE_LOG_CATEGORY_STATIC(LogGPUScene);

// See GPUSceneItem in common.hlsl
struct GPUSceneItem
{
	Float4x4 modelTransform; // localToWorld

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

	materialCBVMaxCounts.resize(swapchainCount, 0);
	materialSRVMaxCounts.resize(swapchainCount, 0);
	materialCBVActualCounts.resize(swapchainCount, 0);
	materialSRVActualCounts.resize(swapchainCount, 0);
	materialCBVMemory.initialize(swapchainCount);
	materialCBVHeap.initialize(swapchainCount);
	materialSRVHeap.initialize(swapchainCount);
	materialCBVs.initialize(swapchainCount);
	materialSRVs.initialize(swapchainCount);

	// Shader
	gpuSceneShader = UniquePtr<ShaderStage>(gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneCS"));
	gpuSceneShader->declarePushConstants({ "pushConstants" });
	gpuSceneShader->loadFromFile(L"gpu_scene.hlsl", "mainCS");

	pipelineState = UniquePtr<PipelineState>(gRenderDevice->createComputePipelineState(
		ComputePipelineDesc{
			.cs = gpuSceneShader.get(),
			.nodeMask = 0,
		}
	));
}

void GPUScene::renderGPUScene(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const SceneProxy* scene,
	const Camera* camera,
	ConstantBufferView* sceneUniform)
{
	const uint32 LOD = 0; // #todo-lod: LOD

	uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();
	uint32 numMeshSections = 0;
	uint32 numDirtyMeshSections = 0;
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		const StaticMesh* sm = scene->staticMeshes[i];
		uint32 currentSections = (uint32)(sm->getSections(LOD).size());
		numMeshSections += currentSections;
		if (sm->isTransformDirty())
		{
			numDirtyMeshSections += currentSections;
		}
	}

	const bool bRebuildGPUScene = scene->bRebuildGPUScene;
	uint32 numGPUSceneCommands = bRebuildGPUScene ? numMeshSections : numDirtyMeshSections;
	if (numGPUSceneCommands > gpuSceneMaxElements)
	{
		resizeGPUSceneBuffer(commandList, numMeshSections);
	}
	resizeGPUSceneCommandBuffer(swapchainIndex, numGPUSceneCommands);
	// #todo-gpuscene: Don't assume material_max_count == mesh_section_total_count.
	resizeMaterialBuffers(swapchainIndex, numMeshSections, numMeshSections);

	uint32 requiredVolatiles = 0;
	requiredVolatiles += 1; // sceneUniform
	requiredVolatiles += 1; // gpuSceneBuffer
	requiredVolatiles += 1; // commandBuffer
	resizeVolatileHeaps(swapchainIndex, requiredVolatiles);

	// #todo-gpuscene: Don't upload unchanged materials. Also don't copy one by one.
	// Prepare bindless materials.
	uint32& currentMaterialCBVCount = materialSRVActualCounts[swapchainIndex];
	uint32& currentMaterialSRVCount = materialCBVActualCounts[swapchainIndex];
	currentMaterialCBVCount = 0;
	currentMaterialSRVCount = 0;
	{
		char eventString[128];
		sprintf_s(eventString, "UpdateMaterialBuffer (count=%u)", numMeshSections);
		SCOPED_DRAW_EVENT_STRING(commandList, eventString);

		auto& CBVs = materialCBVs[swapchainIndex];
		auto& SRVs = materialSRVs[swapchainIndex];
		DescriptorHeap* srvHeap = materialSRVHeap.at(swapchainIndex);

		// #todo-gpuscene: Can't do this in resizeMaterialBuffers()
		srvHeap->resetAllDescriptors();
		SRVs.clear();
		SRVs.reserve(numMeshSections);

		for (uint32 i = 0; i < numStaticMeshes; ++i)
		{
			StaticMesh* staticMesh = scene->staticMeshes[i];
			for (const StaticMeshSection& section : staticMesh->getSections(LOD))
			{
				Material* const material = section.material.get();

				// SRV
				auto albedo = gTextureManager->getSystemTextureGrey2D()->getGPUResource();
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

				// CBV
				MaterialConstants constants;
				if (material != nullptr)
				{
					memcpy_s(constants.albedoMultiplier, sizeof(constants.albedoMultiplier),
						material->albedoMultiplier, sizeof(material->albedoMultiplier));
					constants.roughness = material->roughness;
					constants.emission = material->emission;
				}
				constants.albedoTextureIndex = currentMaterialSRVCount;

				ConstantBufferView* cbv = CBVs[currentMaterialCBVCount].get();
				cbv->writeToGPU(commandList, &constants, sizeof(constants));

				// #todo-gpuscene: Currently always increment even if duplicate items are generated.
				++currentMaterialSRVCount;
				++currentMaterialCBVCount;
			}
		}
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
		StaticMesh* sm = scene->staticMeshes[i];
		const uint32 smSections = (uint32)(sm->getSections(LOD).size());

		if (bRebuildGPUScene == false && sm->isTransformDirty() == false)
		{
			sceneItemIx += smSections;
			continue;
		}

		const Float4x4 localToWorld = sm->getTransformMatrix();
		
		for (uint32 j = 0; j < smSections; ++j)
		{
			const StaticMeshSection& section = sm->getSections(LOD)[j];
			sceneCommands[sceneCommandIx].commandType                       = (uint32)EGPUSceneCommandType::Update;
			sceneCommands[sceneCommandIx].sceneItemIndex                    = sceneItemIx;
			sceneCommands[sceneCommandIx].sceneItem.modelTransform          = localToWorld;
			sceneCommands[sceneCommandIx].sceneItem.localMinBounds          = section.localBounds.minBounds;
			// #todo: uint64 offset
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

		gpuSceneCommandBuffer[swapchainIndex]->singleWriteToGPU(
			commandList,
			sceneCommands.data(),
			(uint32)(sizeof(GPUSceneCommand) * numSceneCommands),
			0);

		BufferMemoryBarrier barriersBefore[] = {
			{
				EBufferMemoryLayout::COMMON,
				EBufferMemoryLayout::PIXEL_SHADER_RESOURCE,
				gpuSceneCommandBuffer.at(swapchainIndex),
			},
			{
				EBufferMemoryLayout::COMMON,
				EBufferMemoryLayout::UNORDERED_ACCESS,
				gpuSceneBuffer.get(),
			},
		};
		commandList->resourceBarriers(_countof(barriersBefore), barriersBefore, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.pushConstant("pushConstants", numSceneCommands);
		SPT.rwStructuredBuffer("gpuSceneBuffer", gpuSceneBufferUAV.get());
		SPT.structuredBuffer("commandBuffer", gpuSceneCommandBufferSRV.at(swapchainIndex));

		commandList->setPipelineState(pipelineState.get());
		commandList->bindComputeShaderParameters(gpuSceneShader.get(), &SPT, volatileViewHeap.at(swapchainIndex));
		commandList->dispatchCompute(numSceneCommands, 1, 1);

		BufferMemoryBarrier barriersAfter[] = {
			{
				EBufferMemoryLayout::UNORDERED_ACCESS,
				EBufferMemoryLayout::PIXEL_SHADER_RESOURCE,
				gpuSceneBuffer.get(),
			},
		};
		commandList->resourceBarriers(_countof(barriersAfter), barriersAfter, 0, nullptr);
	}
}

ShaderResourceView* GPUScene::getGPUSceneBufferSRV() const
{
	return gpuSceneBufferSRV.get();
}

void GPUScene::queryMaterialDescriptorsCount(uint32 swapchainIndex, uint32& outCBVCount, uint32& outSRVCount)
{
	outCBVCount = materialCBVActualCounts[swapchainIndex];
	outSRVCount = materialSRVActualCounts[swapchainIndex];
}

void GPUScene::copyMaterialDescriptors(
	uint32 swapchainIndex,
	DescriptorHeap* destHeap, uint32 destBaseIndex,
	uint32& outCBVBaseIndex, uint32& outCBVCount,
	uint32& outSRVBaseIndex, uint32& outSRVCount,
	uint32& outNextAvailableIndex)
{
	uint32 currentMaterialCBVCount, currentMaterialSRVCount;
	queryMaterialDescriptorsCount(swapchainIndex, currentMaterialCBVCount, currentMaterialSRVCount);

	outCBVBaseIndex = destBaseIndex;
	outCBVCount = currentMaterialCBVCount;

	outSRVBaseIndex = destBaseIndex + currentMaterialCBVCount;
	outSRVCount = currentMaterialSRVCount;

	outNextAvailableIndex = outSRVBaseIndex + outSRVCount;

	if (currentMaterialCBVCount > 0)
	{
		// #todo-rhi: It assumes continuous gRenderDevice->createCBV() calls result in
		// continuous descriptor indices in the same heap.
		// But once I implement dealloc mechanism for descriptor allocation that assumption might break.
		// I need something like gRenderDevice->createCBVsContiuous() that guarantees
		// continuous allocation.
		ConstantBufferView* firstCBV = materialCBVs[swapchainIndex][0].get();
		gRenderDevice->copyDescriptors(
			currentMaterialCBVCount,
			destHeap, outCBVBaseIndex,
			materialCBVHeap[swapchainIndex].get(), firstCBV->getDescriptorIndexInHeap());
	}

	if (currentMaterialSRVCount > 0)
	{
		gRenderDevice->copyDescriptors(
			currentMaterialSRVCount,
			destHeap, outSRVBaseIndex,
			materialSRVHeap[swapchainIndex].get(), 0);
	}
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
void GPUScene::resizeMaterialBuffers(uint32 swapchainIndex, uint32 maxCBVCount, uint32 maxSRVCount)
{
	auto align = [](uint32 size, uint32 alignment) -> uint32
	{
		return (size + (alignment - 1)) & ~(alignment - 1);
	};

	if (materialCBVMaxCounts[swapchainIndex] < maxCBVCount)
	{
		materialCBVMaxCounts[swapchainIndex] = maxCBVCount;

		// #todo-rhi: D3D12-specific alignments
		uint32 materialMemorySize = align(sizeof(MaterialConstants), 256) * maxCBVCount;
		materialMemorySize = align(materialMemorySize, 65536);

		CYLOG(LogGPUScene, Log, L"Resize material constants memory [%u]: %u bytes (%.3f MiB)",
			swapchainIndex, materialMemorySize, (float)materialMemorySize / (1024.0f * 1024.0f));

		materialCBVMemory[swapchainIndex] = UniquePtr<Buffer>(gRenderDevice->createBuffer(
			BufferCreateParams{
				.sizeInBytes = materialMemorySize,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC,
			}
		));

		materialCBVHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV,
				.numDescriptors = maxCBVCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
			}
		));

		uint32 cbMemoryOffset = 0;
		auto& CBVs = materialCBVs[swapchainIndex];

		CBVs.resize(maxCBVCount);
		for (size_t i = 0; i < CBVs.size(); ++i)
		{
			CBVs[i] = UniquePtr<ConstantBufferView>(
				gRenderDevice->createCBV(
					materialCBVMemory.at(swapchainIndex),
					materialCBVHeap.at(swapchainIndex),
					sizeof(MaterialConstants),
					cbMemoryOffset));
			// #todo-rhi: Let RHI layer handle this alignment
			cbMemoryOffset += align(sizeof(MaterialConstants), 256);
		}
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
