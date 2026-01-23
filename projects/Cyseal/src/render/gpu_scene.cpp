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

	materialPassDescriptor.initialize(L"GPUSceneMaterial", swapchainCount, 0);
	materialCommandBuffer.initialize(swapchainCount);
	materialCommandSRV.initialize(swapchainCount);

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

	resizeMaterialBuffer(commandList, maxElements);
	resizeBindlessTextures(commandList, maxElements);
	resizeMaterialCommandBuffer(swapchainIndex, scene);
	
	executeGPUSceneCommands(commandList, swapchainIndex, scene);
	executeMaterialCommands(commandList, swapchainIndex, scene);
}

ShaderResourceView* GPUScene::getGPUSceneBufferSRV() const
{
	return gpuSceneBufferSRV.get();
}

GPUScene::MaterialDescriptorsDesc GPUScene::queryMaterialDescriptors() const
{
	return MaterialDescriptorsDesc{
		.constantsBufferSRV = materialConstantsSRV.get(),
		.srvHeap            = bindlessTextureHeap.get(),
		.srvCount           = bindlessTextureHeap ? bindlessTextureHeap->getCreateParams().numDescriptors : 0,
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

void GPUScene::resizeGPUSceneCommandBuffers(uint32 swapchainIndex, const SceneProxy* scene)
{
	auto fn = [device = this->device](UniquePtr<Buffer>& buffer, UniquePtr<ShaderResourceView>& srv,
		size_t stride, size_t count, const wchar_t* debugName)
	{
		if (count == 0)
		{
			buffer.reset();
			srv.reset();
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

void GPUScene::resizeMaterialBuffer(RenderCommandList* commandList, uint32 maxElements)
{
	if (materialBufferMaxElements >= maxElements)
	{
		return;
	}
	materialBufferMaxElements = maxElements;
	const uint32 stride = sizeof(MaterialConstants);

	Buffer* oldBuffer = nullptr;
	if (materialConstantsBuffer != nullptr)
	{
		oldBuffer = materialConstantsBuffer.release();
		auto oldSRV = materialConstantsSRV.release();
		auto oldUAV = materialConstantsUAV.release();
		oldBuffer->setDebugName(L"Buffer_MaterialConstants_MarkedForDeath");
		commandList->enqueueDeferredDealloc(oldBuffer);
		commandList->enqueueDeferredDealloc(oldSRV);
		commandList->enqueueDeferredDealloc(oldUAV);
	}

	materialConstantsBuffer = UniquePtr<Buffer>(device->createBuffer(
		BufferCreateParams{
			.sizeInBytes = stride * materialBufferMaxElements,
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::UAV,
		}
	));
	materialConstantsBuffer->setDebugName(L"Buffer_MaterialConstants");

	uint64 bufferSize = materialConstantsBuffer->getCreateParams().sizeInBytes;
	CYLOG(LogGPUScene, Log, L"Resize MaterialConstants buffer: %llu bytes (%.3f MiB)",
		bufferSize, (double)bufferSize / (1024.0f * 1024.0f));

	if (oldBuffer != nullptr)
	{
		BufferBarrierAuto barriers[] = {
			{ EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, oldBuffer },
			{ EBarrierSync::COPY, EBarrierAccess::COPY_DEST, materialConstantsBuffer.get() },
		};
		commandList->barrierAuto(_countof(barriers), barriers, 0, nullptr, 0, nullptr);

		size_t oldSize = oldBuffer->getCreateParams().sizeInBytes;
		size_t newSize = materialConstantsBuffer->getCreateParams().sizeInBytes;
		size_t copySize = std::min(oldSize, newSize);
		commandList->copyBufferRegion(oldBuffer, 0, copySize, materialConstantsBuffer.get(), 0);
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
	materialConstantsSRV = UniquePtr<ShaderResourceView>(
		device->createSRV(materialConstantsBuffer.get(), srvDesc));

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
	materialConstantsUAV = UniquePtr<UnorderedAccessView>(
		device->createUAV(materialConstantsBuffer.get(), uavDesc));
}

void GPUScene::resizeBindlessTextures(RenderCommandList* commandList, uint32 maxElements)
{
	if (bindlessTextureHeap == nullptr
		|| bindlessTextureHeap->getCreateParams().numDescriptors < maxElements)
	{
		DescriptorHeap* oldHeap = bindlessTextureHeap.release();
		std::vector<UniquePtr<ShaderResourceView>> oldSRVs = std::move(bindlessSRVs);

		bindlessTextureHeap = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::SRV,
				.numDescriptors = maxElements,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
				.purpose        = EDescriptorHeapPurpose::Volatile,
			}
		));
		bindlessSRVs = std::vector<UniquePtr<ShaderResourceView>>(maxElements);

		if (oldHeap != nullptr)
		{
			const uint32 oldDescCount = oldHeap->getCreateParams().numDescriptors;
			device->copyDescriptors(oldDescCount, bindlessTextureHeap.get(), 0, oldHeap, 0);
			bindlessTextureHeap->internal_copyAllDescriptorIndicesFrom(oldHeap);

			CHECK(oldSRVs.size() == oldDescCount);
			for (size_t i = 0; i < oldDescCount; ++i)
			{
				ShaderResourceView* oldSRV = oldSRVs[i].release();
				if (oldSRV == nullptr) continue;
				bindlessSRVs[i] = UniquePtr<ShaderResourceView>(device->internal_cloneSRVWithDifferentHeap(oldSRV, bindlessTextureHeap.get()));
				commandList->enqueueDeferredDealloc(oldSRV);
			}
			commandList->enqueueDeferredDealloc(oldHeap);
		}
	}
}

void GPUScene::resizeMaterialCommandBuffer(uint32 swapchainIndex, const SceneProxy* scene)
{
	auto fn = [device = this->device](UniquePtr<Buffer>& buffer, UniquePtr<ShaderResourceView>& srv,
		size_t stride, size_t count, const wchar_t* debugName)
	{
		if (count == 0)
		{
			buffer.reset();
			srv.reset();
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
	swprintf_s(debugName, L"Buffer_GPUSceneMaterialCommand_%u", swapchainIndex);
	fn(materialCommandBuffer[swapchainIndex], materialCommandSRV[swapchainIndex],
		sizeof(GPUSceneMaterialCommand), scene->gpuSceneMaterialCommands.size(), debugName);
}

void GPUScene::executeMaterialCommands(RenderCommandList* commandList, uint32 swapchainIndex, const SceneProxy* scene)
{
	SCOPED_DRAW_EVENT(commandList, ExecuteGPUSceneMaterialCommands);

	std::vector<BufferBarrierAuto> barriersBefore = {
		{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, materialConstantsBuffer.get() },
	};
	Buffer* buffers[] = {
		materialCommandBuffer.at(swapchainIndex),
	};
	for (size_t i = 0; i < _countof(buffers); ++i)
	{
		if (buffers[i] == nullptr) continue;
		barriersBefore.push_back({ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, buffers[i] });
	}
	commandList->barrierAuto((uint32)barriersBefore.size(), barriersBefore.data(), 0, nullptr, 0, nullptr);

	// ---------------------------------------------------------------------
	// Evict materials. Simply reset SRVs.

	for (size_t i = 0; i < scene->gpuSceneEvictMaterialCommands.size(); ++i)
	{
		const GPUSceneEvictMaterialCommand& cmd = scene->gpuSceneEvictMaterialCommands[i];
		const uint32 itemIx = cmd.sceneItemIndex;
		bindlessSRVs[itemIx].reset();
	}

	// Deep copy to modify albedoTextureIndex... Wastful, but don't wanna break constness for now.
	std::vector<GPUSceneMaterialCommand> materialCommands = scene->gpuSceneMaterialCommands;

	// Update albedo SRVs.
	{
		Texture* fallback = gTextureManager->getSystemTextureGrey2D()->getGPUResource().get();
		DescriptorHeap* albedoHeap = bindlessTextureHeap.get();

		for (size_t i = 0; i < materialCommands.size(); ++i)
		{
			Texture* albedo = scene->gpuSceneAlbedoTextures[i];
			if (albedo == nullptr) albedo = fallback;

			const uint32 itemIx = materialCommands[i].sceneItemIndex;

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
			auto albedoSRV = device->createSRV(albedo, albedoHeap, srvDesc);

			bindlessSRVs[itemIx] = UniquePtr<ShaderResourceView>(albedoSRV);
			materialCommands[i].materialData.albedoTextureIndex = albedoSRV->getDescriptorIndexInHeap();
		}
	}

	// ---------------------------------------------------------------------
	// Update material buffer.

	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // material buffer
		requiredVolatiles += 1; // command buffer
		materialPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	}

	auto materialBufferUAV = materialConstantsUAV.get();
	auto descriptorHeap = materialPassDescriptor.getDescriptorHeap(swapchainIndex);

	auto fn = [commandList, descriptorHeap, materialBufferUAV]<typename TCommandType>(
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
			SPT.rwStructuredBuffer("materialBuffer", materialBufferUAV);
			SPT.structuredBuffer("commandBuffer", sceneCommandSRV);

			commandList->setComputePipelineState(pipelineState);
			commandList->bindComputeShaderParameters(pipelineState, &SPT, descriptorHeap);
			commandList->dispatchCompute(count, 1, 1);
		}
	};

	fn(materialCommands, materialCommandBuffer.at(swapchainIndex),
		materialCommandSRV.at(swapchainIndex), materialPipelineState.get(),
		"GPUSceneUpdateMaterials");

	BufferBarrierAuto barriersAfter[] = {
		{ EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, materialConstantsBuffer.get() },
	};
	commandList->barrierAuto(_countof(barriersAfter), barriersAfter, 0, nullptr, 0, nullptr);
}
