#include "gpu_scene.h"
#include "static_mesh.h"
#include "material.h"
#include "rhi/gpu_resource.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
#include "rhi/texture_manager.h"
#include "core/matrix.h"
#include "world/scene.h"
#include "util/logging.h"

DEFINE_LOG_CATEGORY_STATIC(LogGPUScene);

#define DEFAULT_MAX_SCENE_ELEMENTS   256

namespace RootParameters
{
	enum Value
	{
		PushConstantsSlot = 0,
		GPUSceneSlot,
		CulledGPUSceneSlot,
		Count
	};
}

// See MeshData in common.hlsl
struct GPUSceneItem
{
	Float4x4 modelTransform; // localToWorld
	uint32   positionBufferOffset;
	uint32   nonPositionBufferOffset;
	uint32   indexBufferOffset;
	uint32   _pad0;
};

void GPUScene::initialize()
{
	resizeGPUSceneBuffers(DEFAULT_MAX_SCENE_ELEMENTS);
	resizeMaterialBuffers(DEFAULT_MAX_SCENE_ELEMENTS, DEFAULT_MAX_SCENE_ELEMENTS);

	// Root signature
	{
		RootParameter rootParameters[RootParameters::Count];
		rootParameters[RootParameters::PushConstantsSlot].initAsConstants(0, 0, 1); // register(b0, space0) = numElements
		rootParameters[RootParameters::GPUSceneSlot].initAsUAV(0, 0);               // register(u0, space0)
		rootParameters[RootParameters::CulledGPUSceneSlot].initAsUAV(1, 0);         // register(u1, space0)

		RootSignatureDesc rootSigDesc(
			RootParameters::Count,
			rootParameters,
			0, nullptr,
			ERootSignatureFlags::None);
		rootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(rootSigDesc));
	}

	// Shader
	ShaderStage* shaderCS = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneCS");
	shaderCS->loadFromFile(L"gpu_scene.hlsl", "mainCS");

	// PSO
	pipelineState = std::unique_ptr<PipelineState>(gRenderDevice->createComputePipelineState(
		ComputePipelineDesc{
			.rootSignature = rootSignature.get(),
			.cs            = shaderCS,
			.nodeMask      = 0
		}
	));
}

void GPUScene::renderGPUScene(RenderCommandList* commandList, const SceneProxy* scene, const Camera* camera)
{
	const uint32 LOD = 0; // #todo-lod: LOD
	const uint32 swapchainIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();
	uint32 numMeshSections = 0;
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		numMeshSections += (uint32)(scene->staticMeshes[i]->getSections(LOD).size());
	}

	// Prepare bindless materials
	currentMaterialSRVCount = 0;
	currentMaterialCBVCount = 0;
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		StaticMesh* staticMesh = scene->staticMeshes[i];
		for (const StaticMeshSection& section : staticMesh->getSections(LOD))
		{
			Material* const material = section.material;

			// SRV
			Texture* albedo = gTextureManager->getSystemTextureGrey2D();
			if (material != nullptr)
			{
				albedo = material->albedoTexture;
			}

			gRenderDevice->copyDescriptors(
				1,
				materialSRVHeap.get(), currentMaterialSRVCount,
				albedo->getSourceSRVHeap(), albedo->getSRVDescriptorIndex());

			// CBV
			MaterialConstants constants;
			if (material != nullptr)
			{
				memcpy_s(constants.albedoMultiplier, sizeof(constants.albedoMultiplier),
					material->albedoMultiplier, sizeof(material->albedoMultiplier));
				constants.roughness = material->roughness;
			}
			constants.albedoTextureIndex = currentMaterialSRVCount;

			ConstantBufferView* cbv = materialCBVs[currentMaterialCBVCount].get();
			cbv->upload(&constants, sizeof(constants), swapchainIndex);

			// #todo-wip: Currently always increment even if duplicate items are generated.
			++currentMaterialSRVCount;
			++currentMaterialCBVCount;
		}
	}

	if (numMeshSections > gpuSceneMaxElements) {
		resizeGPUSceneBuffers(numMeshSections);
		resizeMaterialBuffers(numMeshSections, numMeshSections);
	}
	
	// #todo-wip: Skip upload if scene has not changed.
	// There are various cases:
	// (1) A new object is added to the scene.
	// (2) An object is removed from the scene.
	// (3) No addition or removal but some objects changed their transforms.
	std::vector<GPUSceneItem> sceneData(numMeshSections);
	uint32 k = 0;
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		StaticMesh* sm = scene->staticMeshes[i];
		uint32 smSections = (uint32)(sm->getSections(LOD).size());
		for (uint32 j = 0; j < smSections; ++j)
		{
			const StaticMeshSection& section = sm->getSections(LOD)[j];
			sceneData[k].modelTransform = sm->getTransformMatrix();
			// #todo: uint64 offset
			sceneData[k].positionBufferOffset    = (uint32)section.positionBuffer->getBufferOffsetInBytes();
			sceneData[k].nonPositionBufferOffset = (uint32)section.nonPositionBuffer->getBufferOffsetInBytes();
			sceneData[k].indexBufferOffset       = (uint32)section.indexBuffer->getBufferOffsetInBytes();
			++k;
		}
	}
	gpuSceneBuffer->singleWriteToGPU(
		commandList,
		sceneData.data(),
		(uint32)(sizeof(GPUSceneItem) * sceneData.size()),
		0);

	ResourceBarrier barriersBefore[] = {
		{
			EResourceBarrierType::Transition,
			gpuSceneBuffer.get(),
			EGPUResourceState::COMMON,
			EGPUResourceState::UNORDERED_ACCESS
		},
		{
			EResourceBarrierType::Transition,
			culledGpuSceneBuffer.get(),
			EGPUResourceState::COMMON,
			EGPUResourceState::UNORDERED_ACCESS
		}
	};
	commandList->resourceBarriers(_countof(barriersBefore), barriersBefore);

	commandList->setPipelineState(pipelineState.get());
	commandList->setComputeRootSignature(rootSignature.get());

	commandList->setComputeRootConstant32(RootParameters::PushConstantsSlot, numMeshSections, 0);
	commandList->setComputeRootDescriptorUAV(RootParameters::GPUSceneSlot, gpuSceneBufferUAV.get());
	commandList->setComputeRootDescriptorUAV(RootParameters::CulledGPUSceneSlot, culledGpuSceneBufferUAV.get());

	commandList->dispatchCompute(numMeshSections, 1, 1);

	ResourceBarrier barriersAfter[] = {
		{
			EResourceBarrierType::Transition,
			gpuSceneBuffer.get(),
			EGPUResourceState::UNORDERED_ACCESS,
			EGPUResourceState::PIXEL_SHADER_RESOURCE
		},
		{
			EResourceBarrierType::Transition,
			culledGpuSceneBuffer.get(),
			EGPUResourceState::UNORDERED_ACCESS,
			EGPUResourceState::PIXEL_SHADER_RESOURCE
		}
	};
	commandList->resourceBarriers(_countof(barriersAfter), barriersAfter);
}

ShaderResourceView* GPUScene::getGPUSceneBufferSRV() const
{
	return gpuSceneBufferSRV.get();
}

ShaderResourceView* GPUScene::getCulledGPUSceneBufferSRV() const
{
	return culledGpuSceneBufferSRV.get();
}

void GPUScene::queryMaterialDescriptorsCount(uint32& outCBVCount, uint32& outSRVCount)
{
	outCBVCount = currentMaterialCBVCount;
	outSRVCount = currentMaterialSRVCount;
}

void GPUScene::copyMaterialDescriptors(
	DescriptorHeap* destHeap, uint32 destBaseIndex,
	uint32& outCBVBaseIndex, uint32& outCBVCount,
	uint32& outSRVBaseIndex, uint32& outSRVCount,
	uint32& outNextAvailableIndex)
{
	const uint32 swapchainIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	outCBVBaseIndex = destBaseIndex;
	outCBVCount = currentMaterialCBVCount;

	outSRVBaseIndex = destBaseIndex + currentMaterialCBVCount;
	outSRVCount = currentMaterialSRVCount;

	outNextAvailableIndex = outSRVBaseIndex + outSRVCount;

	// #todo-wip: Can't copy descriptors at once because
	// CBVs for the same swapchain index are not contiguous in memory.
	for (uint32 i = 0; i < currentMaterialCBVCount; ++i)
	{
		ConstantBufferView* cbv = materialCBVs[i].get();
		gRenderDevice->copyDescriptors(
			1,
			destHeap, outCBVBaseIndex + i,
			cbv->getSourceHeap(), cbv->getDescriptorIndexInHeap(swapchainIndex));
	}

	gRenderDevice->copyDescriptors(
		currentMaterialSRVCount,
		destHeap, outSRVBaseIndex,
		materialSRVHeap.get(), 0);
}

void GPUScene::resizeGPUSceneBuffers(uint32 maxElements)
{
	gpuSceneMaxElements = maxElements;
	const uint32 viewStride = sizeof(GPUSceneItem);

	gpuSceneBuffer = std::unique_ptr<Buffer>(gRenderDevice->createBuffer(
		BufferCreateParams{
			.sizeInBytes = viewStride * gpuSceneMaxElements,
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::CPU_WRITE | EBufferAccessFlags::UAV,
		}
	));
	culledGpuSceneBuffer = std::unique_ptr<Buffer>(gRenderDevice->createBuffer(
		BufferCreateParams{
			.sizeInBytes = viewStride * gpuSceneMaxElements,
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::UAV,
		}
	));
	
	{
		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::UNKNOWN;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = gpuSceneMaxElements;
		srvDesc.buffer.structureByteStride = viewStride;
		srvDesc.buffer.flags               = EBufferSRVFlags::None;
		gpuSceneBufferSRV = std::unique_ptr<ShaderResourceView>(
			gRenderDevice->createSRV(gpuSceneBuffer.get(), srvDesc));
	}
	{
		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::UNKNOWN;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = gpuSceneMaxElements;
		srvDesc.buffer.structureByteStride = viewStride;
		srvDesc.buffer.flags               = EBufferSRVFlags::None;
		culledGpuSceneBufferSRV = std::unique_ptr<ShaderResourceView>(
			gRenderDevice->createSRV(culledGpuSceneBuffer.get(), srvDesc));
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
		gpuSceneBufferUAV = std::unique_ptr<UnorderedAccessView>(
			gRenderDevice->createUAV(gpuSceneBuffer.get(), uavDesc));
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
		culledGpuSceneBufferUAV = std::unique_ptr<UnorderedAccessView>(
			gRenderDevice->createUAV(culledGpuSceneBuffer.get(), uavDesc));
	}
}

// Bindless materials
void GPUScene::resizeMaterialBuffers(uint32 maxCBVCount, uint32 maxSRVCount)
{
	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	auto align = [](uint32 size, uint32 alignment) -> uint32
	{
		return (size + (alignment - 1)) & ~(alignment - 1);
	};

	uint32 materialMemoryPoolSize = align(sizeof(MaterialConstants), 256) * maxCBVCount * swapchainCount;
	materialMemoryPoolSize = align(materialMemoryPoolSize, 65536);

	CYLOG(LogGPUScene, Log, L"Resize material constants memory: %u bytes (%.3f MiB)",
		materialMemoryPoolSize, (float)materialMemoryPoolSize / (1024.0f * 1024.0f));

	materialCBVMemory = std::unique_ptr<ConstantBuffer>(
		gRenderDevice->createConstantBuffer(materialMemoryPoolSize));

	materialCBVHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::CBV,
			.numDescriptors = maxCBVCount * swapchainCount,
			.flags          = EDescriptorHeapFlags::None,
			.nodeMask       = 0,
		}
	));

	materialSRVHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::SRV,
			.numDescriptors = maxSRVCount * swapchainCount,
			.flags          = EDescriptorHeapFlags::None,
			.nodeMask       = 0,
		}
	));

	materialCBVs.resize(maxCBVCount);
	for (size_t i = 0; i < materialCBVs.size(); ++i)
	{
		materialCBVs[i] = std::unique_ptr<ConstantBufferView>(
			materialCBVMemory->allocateCBV(materialCBVHeap.get(), sizeof(MaterialConstants), swapchainCount));
	}
}
