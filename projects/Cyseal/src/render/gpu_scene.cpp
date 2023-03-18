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
#include "world/gpu_resource_asset.h"
#include "util/logging.h"

DEFINE_LOG_CATEGORY_STATIC(LogGPUScene);

#define DEFAULT_MAX_SCENE_ELEMENTS   256

namespace RootParameters
{
	enum Value
	{
		PushConstantsSlot = 0,
		SceneUniformSlot,
		GPUSceneSlot,
		Count
	};
}

// See MeshData in common.hlsl
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

void GPUScene::initialize()
{
	resizeGPUSceneBuffers(DEFAULT_MAX_SCENE_ELEMENTS);
	resizeMaterialBuffers(DEFAULT_MAX_SCENE_ELEMENTS, DEFAULT_MAX_SCENE_ELEMENTS);

	// Root signature
	{
		DescriptorRange descriptorRanges[2];
		descriptorRanges[0].init(EDescriptorRangeType::CBV, 1, 1, 0); // register(b1, space0)

		RootParameter rootParameters[RootParameters::Count];
		rootParameters[RootParameters::PushConstantsSlot].initAsConstants(0, 0, 1); // register(b0, space0) = numElements
		rootParameters[RootParameters::SceneUniformSlot].initAsDescriptorTable(1, &descriptorRanges[0]);
		rootParameters[RootParameters::GPUSceneSlot].initAsUAV(0, 0);               // register(u0, space0)

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

	delete shaderCS;
}

void GPUScene::renderGPUScene(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const SceneProxy* scene,
	const Camera* camera,
	ConstantBufferView* sceneUniform)
{
	const uint32 LOD = 0; // #todo-lod: LOD

	auto& materialCBVs = materialCBVsPerFrame[swapchainIndex];

	uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();
	uint32 numMeshSections = 0;
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		numMeshSections += (uint32)(scene->staticMeshes[i]->getSections(LOD).size());
	}

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // scene uniform
		if (requiredVolatiles > totalVolatileDescriptors)
		{
			resizeVolatileHeaps(requiredVolatiles);
		}
	}

	// Prepare bindless materials
	currentMaterialSRVCount = 0;
	currentMaterialCBVCount = 0;
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
			cbv->writeToGPU(commandList, &constants, sizeof(constants));

			// #todo-wip: Currently always increment even if duplicate items are generated.
			++currentMaterialSRVCount;
			++currentMaterialCBVCount;
		}
	}

	if (numMeshSections > gpuSceneMaxElements)
	{
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
			sceneData[k].modelTransform          = sm->getTransformMatrix();
			sceneData[k].localMinBounds          = section.localBounds.minBounds;
			// #todo: uint64 offset
			sceneData[k].positionBufferOffset    = (uint32)section.positionBuffer->getGPUResource()->getBufferOffsetInBytes();
			sceneData[k].localMaxBounds          = section.localBounds.maxBounds;
			sceneData[k].nonPositionBufferOffset = (uint32)section.nonPositionBuffer->getGPUResource()->getBufferOffsetInBytes();
			sceneData[k].indexBufferOffset       = (uint32)section.indexBuffer->getGPUResource()->getBufferOffsetInBytes();
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
	};
	commandList->resourceBarriers(_countof(barriersBefore), barriersBefore);

	commandList->setPipelineState(pipelineState.get());
	commandList->setComputeRootSignature(rootSignature.get());

	DescriptorHeap* volatileHeap = volatileViewHeaps[swapchainIndex].get();
	DescriptorHeap* heaps[] = { volatileHeap };
	commandList->setDescriptorHeaps(1, heaps);

	constexpr uint32 VOLATILE_IX_SceneUniform = 0;
	constexpr uint32 VOLATILE_IX_VisibleCounter = 1;
	gRenderDevice->copyDescriptors(
		1,
		volatileHeap, VOLATILE_IX_SceneUniform,
		sceneUniform->getSourceHeap(), sceneUniform->getDescriptorIndexInHeap());

	// Bind root parameters
	commandList->setComputeRootConstant32(RootParameters::PushConstantsSlot, numMeshSections, 0);
	commandList->setComputeRootDescriptorTable(RootParameters::SceneUniformSlot, volatileHeap, VOLATILE_IX_SceneUniform);
	commandList->setComputeRootDescriptorUAV(RootParameters::GPUSceneSlot, gpuSceneBufferUAV.get());

	commandList->dispatchCompute(numMeshSections, 1, 1);

	ResourceBarrier barriersAfter[] = {
		{
			EResourceBarrierType::Transition,
			gpuSceneBuffer.get(),
			EGPUResourceState::UNORDERED_ACCESS,
			EGPUResourceState::PIXEL_SHADER_RESOURCE
		},
	};
	commandList->resourceBarriers(_countof(barriersAfter), barriersAfter);
}

ShaderResourceView* GPUScene::getGPUSceneBufferSRV() const
{
	return gpuSceneBufferSRV.get();
}

void GPUScene::queryMaterialDescriptorsCount(uint32& outCBVCount, uint32& outSRVCount)
{
	outCBVCount = currentMaterialCBVCount;
	outSRVCount = currentMaterialSRVCount;
}

void GPUScene::copyMaterialDescriptors(
	uint32 swapchainIndex,
	DescriptorHeap* destHeap, uint32 destBaseIndex,
	uint32& outCBVBaseIndex, uint32& outCBVCount,
	uint32& outSRVBaseIndex, uint32& outSRVCount,
	uint32& outNextAvailableIndex)
{
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
		ConstantBufferView* firstCBV = materialCBVsPerFrame[swapchainIndex][0].get();
		gRenderDevice->copyDescriptors(
			currentMaterialCBVCount,
			destHeap, outCBVBaseIndex,
			materialCBVHeap.get(), firstCBV->getDescriptorIndexInHeap());
	}

	if (currentMaterialSRVCount > 0)
	{
		gRenderDevice->copyDescriptors(
			currentMaterialSRVCount,
			destHeap, outSRVBaseIndex,
			materialSRVHeap.get(), 0);
	}
}

void GPUScene::resizeVolatileHeaps(uint32 maxDescriptors)
{
	totalVolatileDescriptors = maxDescriptors;

	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	volatileViewHeaps.resize(swapchainCount);
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		volatileViewHeaps[i] = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV_SRV_UAV,
				.numDescriptors = maxDescriptors,
				.flags          = EDescriptorHeapFlags::ShaderVisible,
				.nodeMask       = 0,
			}
		));

		wchar_t debugName[256];
		swprintf_s(debugName, L"GPUScene_VolatileViewHeap_%u", i);
		volatileViewHeaps[i]->setDebugName(debugName);
	}

	CYLOG(LogGPUScene, Log, L"Resize volatile heap: %u descriptors", maxDescriptors);
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
}

// Bindless materials
void GPUScene::resizeMaterialBuffers(uint32 maxCBVCount, uint32 maxSRVCount)
{
	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	auto align = [](uint32 size, uint32 alignment) -> uint32
	{
		return (size + (alignment - 1)) & ~(alignment - 1);
	};

	// #todo-rhi: D3D12-specific alignments
	uint32 materialMemoryPoolSize = align(sizeof(MaterialConstants), 256) * maxCBVCount * swapchainCount;
	materialMemoryPoolSize = align(materialMemoryPoolSize, 65536);

	CYLOG(LogGPUScene, Log, L"Resize material constants memory: %u bytes (%.3f MiB)",
		materialMemoryPoolSize, (float)materialMemoryPoolSize / (1024.0f * 1024.0f));

	materialCBVMemory = std::unique_ptr<Buffer>(gRenderDevice->createBuffer(
		BufferCreateParams{
			.sizeInBytes = materialMemoryPoolSize,
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::CPU_WRITE,
		}
	));

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

	materialCBVsPerFrame.resize(swapchainCount);
	uint32 cbMemoryOffset = 0;
	for (uint32 swapchainIx = 0; swapchainIx < swapchainCount; ++swapchainIx)
	{
		auto& materialCBVs = materialCBVsPerFrame[swapchainIx];
		materialCBVs.resize(maxCBVCount);
		for (size_t i = 0; i < materialCBVs.size(); ++i)
		{
			materialCBVs[i] = std::unique_ptr<ConstantBufferView>(
				gRenderDevice->createCBV(
					materialCBVMemory.get(),
					materialCBVHeap.get(),
					sizeof(MaterialConstants),
					cbMemoryOffset));
			// #todo-rhi: Let RHI layer handle this alignment
			cbMemoryOffset += align(sizeof(MaterialConstants), 256);
		}
	}
}
