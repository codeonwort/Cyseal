#include "gpu_scene.h"
#include "gpu_scene_command.h"
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

void GPUScene::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	gpuSceneCommandBufferMaxElements.resize(swapchainCount);
	gpuSceneCommandBuffer.initialize(swapchainCount);
	gpuSceneCommandBufferSRV.initialize(swapchainCount);

	gpuSceneEvictCommandBuffer.initialize(swapchainCount);
	gpuSceneAllocCommandBuffer.initialize(swapchainCount);
	gpuSceneUpdateCommandBuffer.initialize(swapchainCount);
	gpuSceneEvictCommandBufferSRV.initialize(swapchainCount);
	gpuSceneAllocCommandBufferSRV.initialize(swapchainCount);
	gpuSceneUpdateCommandBufferSRV.initialize(swapchainCount);

	passDescriptor.initialize(L"GPUScene", swapchainCount, 0);

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
	{
		ShaderStage* gpuSceneShader = device->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneCS");
		gpuSceneShader->declarePushConstants({ { "pushConstants", 1} });
		gpuSceneShader->loadFromFile(L"gpu_scene.hlsl", "mainCS");

		pipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = gpuSceneShader, .nodeMask = 0, }
		));

		delete gpuSceneShader; // No use after PSO creation.
	}
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneEvictCS");
		shader->declarePushConstants({ { "pushConstants", 1} });
		shader->loadFromFile(L"gpu_scene.hlsl", "mainCS", { L"COMMAND_TYPE=0" });

		evictPipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0, }
		));

		delete shader; // No use after PSO creation.
	}
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneAllocCS");
		shader->declarePushConstants({ { "pushConstants", 1} });
		shader->loadFromFile(L"gpu_scene.hlsl", "mainCS", { L"COMMAND_TYPE=1" });

		allocPipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0, }
		));

		delete shader; // No use after PSO creation.
	}
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneUpdateCS");
		shader->declarePushConstants({ { "pushConstants", 1} });
		shader->loadFromFile(L"gpu_scene.hlsl", "mainCS", { L"COMMAND_TYPE=2" });

		updatePipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0, }
		));

		delete shader; // No use after PSO creation.
	}
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
	// #wip-material: Don't assume material_max_count == mesh_section_total_count.
	resizeMaterialBuffers(swapchainIndex, numMeshSections, numMeshSections);

	resizeGPUSceneCommandBuffers(commandList, swapchainIndex, scene);

	uint32 requiredVolatiles = 0;
	requiredVolatiles += 1; // sceneUniform
	requiredVolatiles += 1; // gpuSceneBuffer
	requiredVolatiles += 1; // commandBuffer
	resizeVolatileHeaps(swapchainIndex, requiredVolatiles);

	// #wip-material: Don't upload unchanged materials. Also don't copy one by one.
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

		// #wip-material: Can't do this in resizeMaterialBuffers()
		// #wip-material: Destructors are slow here, when we can just wipe out them.
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
				auto albedoSRV = device->createSRV(albedo.get(), srvHeap, srvDesc);
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

				// #wip-material: Currently always increment even if duplicate items are generated.
				++currentMaterialSRVCount;
				++currentConstantsCount;
			}
		}

		uint32 materialDataSize = (uint32)(sizeof(MaterialConstants) * materialConstantsData.size());
		materialConstantsMemory[swapchainIndex]->singleWriteToGPU(commandList, materialConstantsData.data(), materialDataSize, 0);
	}
	
#if 0
	// #wip: Avoid recreation of buffers when bRebuildGPUScene == true.
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
			sceneCommands[sceneCommandIx].commandType    = (uint32)EGPUSceneCommandType::Update;
			sceneCommands[sceneCommandIx].sceneItemIndex = sceneItemIx;
			sceneCommands[sceneCommandIx].sceneItem      = GPUSceneItem{
				.localToWorld            = localToWorld,
				.prevLocalToWorld        = prevLocalToWorld,
				.localMinBounds          = section.localBounds.minBounds,
				.positionBufferOffset    = (uint32)section.positionBuffer->getGPUResource()->getBufferOffsetInBytes(), // #todo-gpuscene: uint64 offset
				.localMaxBounds          = section.localBounds.maxBounds,
				.nonPositionBufferOffset = (uint32)section.nonPositionBuffer->getGPUResource()->getBufferOffsetInBytes(),
				.indexBufferOffset       = (uint32)section.indexBuffer->getGPUResource()->getBufferOffsetInBytes(),
				.flags                   = GPUSceneItem::FlagBits::IsValid,
			};
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

		BufferBarrierAuto barriersBefore[] = {
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, gpuSceneCommandBuffer.at(swapchainIndex) },
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, gpuSceneBuffer.get() },
		};
		commandList->barrierAuto(_countof(barriersBefore), barriersBefore, 0, nullptr, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.pushConstant("pushConstants", numSceneCommands);
		SPT.rwStructuredBuffer("gpuSceneBuffer", gpuSceneBufferUAV.get());
		SPT.structuredBuffer("commandBuffer", gpuSceneCommandBufferSRV.at(swapchainIndex));

		commandList->setComputePipelineState(pipelineState.get());
		commandList->bindComputeShaderParameters(pipelineState.get(), &SPT, volatileViewHeap.at(swapchainIndex));
		commandList->dispatchCompute(numSceneCommands, 1, 1);

		BufferBarrierAuto barriersAfter[] = {
			{ EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, gpuSceneBuffer.get() },
		};
		commandList->barrierAuto(_countof(barriersAfter), barriersAfter, 0, nullptr, 0, nullptr);
	}
#else
	executeGPUSceneCommands(commandList, swapchainIndex, scene);
#endif
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

		volatileViewHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV_SRV_UAV,
				.numDescriptors = maxDescriptors,
				.flags          = EDescriptorHeapFlags::ShaderVisible,
				.nodeMask       = 0,
				.purpose        = EDescriptorHeapPurpose::Volatile,
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

		gpuSceneCommandBuffer[swapchainIndex] = UniquePtr<Buffer>(device->createBuffer(
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
			device->createSRV(gpuSceneCommandBuffer.at(swapchainIndex), srvDesc));
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

	gpuSceneBuffer = UniquePtr<Buffer>(device->createBuffer(
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
			device->createSRV(gpuSceneBuffer.get(), srvDesc));
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
			device->createUAV(gpuSceneBuffer.get(), uavDesc));
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

		materialConstantsMemory[swapchainIndex] = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = materialMemorySize,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC,
			}
		));
		materialConstantsHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::SRV,
				.numDescriptors = maxConstantsCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
				.purpose        = EDescriptorHeapPurpose::Volatile,
			}
		));
		materialConstantsSRV[swapchainIndex] = UniquePtr<ShaderResourceView>(device->createSRV(
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

		materialSRVHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::SRV,
				.numDescriptors = maxSRVCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
				.purpose        = EDescriptorHeapPurpose::Volatile,
			}
		));
	}
}

void GPUScene::resizeGPUSceneCommandBuffers(RenderCommandList* commandList, uint32 swapchainIndex, const SceneProxy* scene)
{
	auto fn = [device = this->device](UniquePtr<Buffer>& buffer, UniquePtr<ShaderResourceView>& srv,
		size_t stride, size_t count, const wchar_t* debugName)
	{
		if (count == 0)
		{
			buffer = nullptr;
			srv = nullptr;
		}
		else if (buffer == nullptr
			|| (buffer->getCreateParams().sizeInBytes / stride) < count
			|| (buffer->getCreateParams().sizeInBytes / stride) > (count * 2))
		{
			buffer = UniquePtr<Buffer>(device->createBuffer(
				BufferCreateParams{
					.sizeInBytes = stride * count,
					.alignment   = 0,
					.accessFlags = EBufferAccessFlags::COPY_SRC | EBufferAccessFlags::SRV
				}
			));
			buffer->setDebugName(debugName);

			ShaderResourceViewDesc srvDesc{
				.format        = EPixelFormat::UNKNOWN,
				.viewDimension = ESRVDimension::Buffer,
				.buffer        = BufferSRVDesc{
					.firstElement        = 0,
					.numElements         = (uint32)count,
					.structureByteStride = (uint32)stride,
					.flags               = EBufferSRVFlags::None,
				}
			};
			srv = UniquePtr<ShaderResourceView>(device->createSRV(buffer.get(), srvDesc));
		}
	};

	wchar_t debugName[256];

	swprintf_s(debugName, L"Buffer_GPUSceneEvictCommand_%u", swapchainIndex);
	fn(gpuSceneEvictCommandBuffer[swapchainIndex], gpuSceneEvictCommandBufferSRV[swapchainIndex],
		sizeof(GPUSceneEvictCommand), scene->gpuSceneEvictCommands.size(), debugName);

	swprintf_s(debugName, L"Buffer_GPUSceneAllocCommand_%u", swapchainIndex);
	fn(gpuSceneAllocCommandBuffer[swapchainIndex], gpuSceneAllocCommandBufferSRV[swapchainIndex],
		sizeof(GPUSceneAllocCommand), scene->gpuSceneAllocCommands.size(), debugName);

	swprintf_s(debugName, L"Buffer_GPUSceneUpdateCommand_%u", swapchainIndex);
	fn(gpuSceneUpdateCommandBuffer[swapchainIndex], gpuSceneUpdateCommandBufferSRV[swapchainIndex],
		sizeof(GPUSceneUpdateCommand), scene->gpuSceneUpdateCommands.size(), debugName);
}

void GPUScene::executeGPUSceneCommands(RenderCommandList* commandList, uint32 swapchainIndex, const SceneProxy* scene)
{
	SCOPED_DRAW_EVENT(commandList, ExecuteGPUSceneCommands);

	std::vector<BufferBarrierAuto> barriersBefore = {
		{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, gpuSceneBuffer.get() }
	};
	Buffer* buffers[] = {
		gpuSceneEvictCommandBuffer.at(swapchainIndex),
		gpuSceneAllocCommandBuffer.at(swapchainIndex),
		gpuSceneUpdateCommandBuffer.at(swapchainIndex),
	};
	for (size_t i = 0; i < _countof(buffers); ++i)
	{
		if (buffers[i] == nullptr) continue;
		barriersBefore.push_back({ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, buffers[i] });
	}
	commandList->barrierAuto((uint32)barriersBefore.size(), barriersBefore.data(), 0, nullptr, 0, nullptr);

	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // gpuSceneBuffer
		requiredVolatiles += 1; // commandBuffer
		passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles * 3);
	}

	auto sceneBufferUAV = gpuSceneBufferUAV.get();
	auto descriptorHeap = passDescriptor.getDescriptorHeap(swapchainIndex);
	DescriptorIndexTracker tracker{};

	auto fn = [commandList, descriptorHeap, sceneBufferUAV, &tracker]<typename TCommandType>(
		const std::vector<TCommandType>& sceneCommands,
		Buffer*                          sceneCommandBuffer,
		ShaderResourceView*              sceneCommandSRV,
		ComputePipelineState*            pipelineState,
		const char*                      drawEventName)
	{
		const uint32 count = (uint32)sceneCommands.size();
		if (count > 0)
		{
			char eventString[128];
			sprintf_s(eventString, "%s (count=%u)", drawEventName, count);
			SCOPED_DRAW_EVENT_STRING(commandList, eventString);

			sceneCommandBuffer->singleWriteToGPU(
				commandList,
				(void*)sceneCommands.data(),
				(uint32)(sizeof(sceneCommands[0]) * count),
				0);

			ShaderParameterTable SPT{};
			SPT.pushConstant("pushConstants", count);
			SPT.rwStructuredBuffer("gpuSceneBuffer", sceneBufferUAV);
			SPT.structuredBuffer("commandBuffer", sceneCommandSRV);

			commandList->setComputePipelineState(pipelineState);
			commandList->bindComputeShaderParameters(pipelineState, &SPT, descriptorHeap, &tracker);
			commandList->dispatchCompute(count, 1, 1);
		}
	};

	fn(scene->gpuSceneEvictCommands, gpuSceneEvictCommandBuffer.at(swapchainIndex),
		gpuSceneEvictCommandBufferSRV.at(swapchainIndex), evictPipelineState.get(),
		"GPUSceneEvictItems");
	fn(scene->gpuSceneAllocCommands, gpuSceneAllocCommandBuffer.at(swapchainIndex),
		gpuSceneAllocCommandBufferSRV.at(swapchainIndex), allocPipelineState.get(),
		"GPUSceneAllocItems");
	fn(scene->gpuSceneUpdateCommands, gpuSceneUpdateCommandBuffer.at(swapchainIndex),
		gpuSceneUpdateCommandBufferSRV.at(swapchainIndex), updatePipelineState.get(),
		"GPUSceneUpdateItems");

	BufferBarrierAuto barriersAfter[] = {
		{ EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, gpuSceneBuffer.get() },
	};
	commandList->barrierAuto(_countof(barriersAfter), barriersAfter, 0, nullptr, 0, nullptr);
}
