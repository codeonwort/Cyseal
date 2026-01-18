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

	gpuSceneEvictCommandBuffer.initialize(swapchainCount);
	gpuSceneAllocCommandBuffer.initialize(swapchainCount);
	gpuSceneUpdateCommandBuffer.initialize(swapchainCount);
	gpuSceneEvictCommandBufferSRV.initialize(swapchainCount);
	gpuSceneAllocCommandBufferSRV.initialize(swapchainCount);
	gpuSceneUpdateCommandBufferSRV.initialize(swapchainCount);

	passDescriptor.initialize(L"GPUScene", swapchainCount, 0);

	materialConstantsMaxCounts.resize(swapchainCount, 0);
	materialSRVMaxCounts.resize(swapchainCount, 0);
	materialConstantsActualCounts.resize(swapchainCount, 0);
	materialSRVActualCounts.resize(swapchainCount, 0);
	materialSRVHeap.initialize(swapchainCount);
	materialSRVs.initialize(swapchainCount);

	materialConstantsMemory.initialize(swapchainCount);
	materialConstantsHeap.initialize(swapchainCount);
	materialConstantsSRV.initialize(swapchainCount);

	materialPassDescriptor.initialize(L"GPUSceneMaterial", swapchainCount, 0);

	// Shaders
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
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneMaterialUpdateCS");
		shader->declarePushConstants({ { "pushConstants", 1} });
		shader->loadFromFile(L"gpu_scene_material.hlsl", "mainCS");

		materialPipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
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

	uint32 numMeshSections = 0;
	uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		StaticMeshProxy* sm = scene->staticMeshes[i];
		numMeshSections += (uint32)(sm->getSections().size());
	}

	if (numMeshSections == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}

	uint32 maxElements = numMeshSections;
	if (scene->gpuSceneItemMaxValidIndex != 0xffffffff && scene->gpuSceneItemMaxValidIndex + 1 > maxElements)
	{
		maxElements = scene->gpuSceneItemMaxValidIndex + 1;
	}
	resizeGPUSceneBuffer(commandList, maxElements);

	resizeGPUSceneCommandBuffers(swapchainIndex, scene);

	// #wip-material: Don't assume material_max_count == mesh_section_total_count.
	resizeMaterialBuffers(swapchainIndex, numMeshSections, numMeshSections);
	resizeMaterialBuffer2(commandList, maxElements);

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
	
	executeGPUSceneCommands(commandList, swapchainIndex, scene);
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

void GPUScene::resizeGPUSceneBuffer(RenderCommandList* commandList, uint32 maxElements)
{
	if (gpuSceneMaxElements >= maxElements)
	{
		return;
	}
	gpuSceneMaxElements = maxElements;
	const uint32 viewStride = sizeof(GPUSceneItem);

	Buffer* oldBuffer = nullptr;
	if (gpuSceneBuffer != nullptr)
	{
		oldBuffer = gpuSceneBuffer.release();
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

	if (oldBuffer != nullptr)
	{
		BufferBarrierAuto barriers[] = {
			{ EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, oldBuffer },
			{ EBarrierSync::COPY, EBarrierAccess::COPY_DEST, gpuSceneBuffer.get() },
		};
		commandList->barrierAuto(_countof(barriers), barriers, 0, nullptr, 0, nullptr);

		size_t oldSize = oldBuffer->getCreateParams().sizeInBytes;
		size_t newSize = gpuSceneBuffer->getCreateParams().sizeInBytes;
		size_t copySize = std::min(oldSize, newSize);
		commandList->copyBufferRegion(oldBuffer, 0, copySize, gpuSceneBuffer.get(), 0);
	}
	
	ShaderResourceViewDesc srvDesc{};
	srvDesc.format                     = EPixelFormat::UNKNOWN;
	srvDesc.viewDimension              = ESRVDimension::Buffer;
	srvDesc.buffer.firstElement        = 0;
	srvDesc.buffer.numElements         = gpuSceneMaxElements;
	srvDesc.buffer.structureByteStride = viewStride;
	srvDesc.buffer.flags               = EBufferSRVFlags::None;
	gpuSceneBufferSRV = UniquePtr<ShaderResourceView>(
		device->createSRV(gpuSceneBuffer.get(), srvDesc));
	
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

		// Destroy SRV early than its DescriptorHeap.
		materialConstantsSRV[swapchainIndex] = nullptr;

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

		materialSRVs[swapchainIndex].clear();

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

void GPUScene::resizeGPUSceneCommandBuffers(uint32 swapchainIndex, const SceneProxy* scene)
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

void GPUScene::resizeMaterialBuffer2(RenderCommandList* commandList, uint32 maxElements)
{
	if (materialBufferMaxElements >= maxElements)
	{
		return;
	}
	materialBufferMaxElements = maxElements;
	const uint32 stride = sizeof(MaterialConstants);

	Buffer* oldBuffer = nullptr;
	if (materialConstantsBuffer2 != nullptr)
	{
		oldBuffer = materialConstantsBuffer2.release();
		auto oldSRV = materialConstantsSRV2.release();
		auto oldUAV = materialConstantsUAV2.release();
		oldBuffer->setDebugName(L"Buffer_MaterialConstants_MarkedForDeath");
		commandList->enqueueDeferredDealloc(oldBuffer);
		commandList->enqueueDeferredDealloc(oldSRV);
		commandList->enqueueDeferredDealloc(oldUAV);
	}

	materialConstantsBuffer2 = UniquePtr<Buffer>(device->createBuffer(
		BufferCreateParams{
			.sizeInBytes = stride * materialBufferMaxElements,
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::UAV,
		}
	));
	materialConstantsBuffer2->setDebugName(L"Buffer_MaterialConstants");

	uint64 bufferSize = materialConstantsBuffer2->getCreateParams().sizeInBytes;
	CYLOG(LogGPUScene, Log, L"Resize MaterialConstants buffer: %llu bytes (%.3f MiB)",
		bufferSize, (double)bufferSize / (1024.0f * 1024.0f));

	if (oldBuffer != nullptr)
	{
		BufferBarrierAuto barriers[] = {
			{ EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, oldBuffer },
			{ EBarrierSync::COPY, EBarrierAccess::COPY_DEST, materialConstantsBuffer2.get() },
		};
		commandList->barrierAuto(_countof(barriers), barriers, 0, nullptr, 0, nullptr);

		size_t oldSize = oldBuffer->getCreateParams().sizeInBytes;
		size_t newSize = materialConstantsBuffer2->getCreateParams().sizeInBytes;
		size_t copySize = std::min(oldSize, newSize);
		commandList->copyBufferRegion(oldBuffer, 0, copySize, materialConstantsBuffer2.get(), 0);
	}
	
	ShaderResourceViewDesc srvDesc{
		.format        = EPixelFormat::UNKNOWN,
		.viewDimension = ESRVDimension::Buffer,
		.buffer        = BufferSRVDesc{
			.firstElement        = 0,
			.numElements         = materialBufferMaxElements,
			.structureByteStride = stride,
			.flags               = EBufferSRVFlags::None,
		}
	};
	materialConstantsSRV2 = UniquePtr<ShaderResourceView>(
		device->createSRV(materialConstantsBuffer2.get(), srvDesc));

	UnorderedAccessViewDesc uavDesc{
		.format        = EPixelFormat::UNKNOWN,
		.viewDimension = EUAVDimension::Buffer,
		.buffer        = BufferUAVDesc{
			.firstElement         = 0,
			.numElements          = materialBufferMaxElements,
			.structureByteStride  = stride,
			.counterOffsetInBytes = 0,
			.flags                = EBufferUAVFlags::None,
		}
	};
	materialConstantsUAV2 = UniquePtr<UnorderedAccessView>(
		device->createUAV(materialConstantsBuffer2.get(), uavDesc));
}
