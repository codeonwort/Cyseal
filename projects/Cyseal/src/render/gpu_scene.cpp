#include "gpu_scene.h"
#include "gpu_resource.h"
#include "render_device.h"
#include "swap_chain.h"
#include "static_mesh.h"
#include "material.h"
#include "texture_manager.h"
#include "core/matrix.h"
#include "world/scene.h"

// #todo-wip: Should not be constants
#define MAX_SCENE_ELEMENTS           256
#define MATERIAL_MEMORY_POOL_SIZE    (640 * 1024) // 640 KiB
#define MAX_MATERIAL_CBVs            (MAX_SCENE_ELEMENTS)
#define MAX_MATERIAL_SRVs            (MAX_SCENE_ELEMENTS)

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
	gpuSceneBuffer = std::unique_ptr<StructuredBuffer>(
		gRenderDevice->createStructuredBuffer(
			MAX_SCENE_ELEMENTS,
			sizeof(GPUSceneItem),
			EBufferAccessFlags::CPU_WRITE | EBufferAccessFlags::UAV));
	culledGpuSceneBuffer = std::unique_ptr<StructuredBuffer>(
		gRenderDevice->createStructuredBuffer(
			MAX_SCENE_ELEMENTS,
			sizeof(GPUSceneItem),
			EBufferAccessFlags::UAV));

	// Root signature
	{
		constexpr uint32 NUM_ROOT_PARAMETERS = 3;
		RootParameter rootParameters[NUM_ROOT_PARAMETERS];
		rootParameters[0].initAsConstants(0 /*b0*/, 0, 1); // numElements
		rootParameters[1].initAsUAV(0 /*u0 */, 0);         // gpuSceneBuffer
		rootParameters[2].initAsUAV(1 /*u1 */, 0);         // culledGpuSceneBuffer

		RootSignatureDesc rootSigDesc(
			NUM_ROOT_PARAMETERS,
			rootParameters,
			0, nullptr,
			ERootSignatureFlags::None);
		rootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(rootSigDesc));
	}

	// Shader
	ShaderStage* shaderCS = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneCS");
	shaderCS->loadFromFile(L"gpu_scene.hlsl", "mainCS");

	// PSO
	{
		ComputePipelineDesc desc;
		desc.rootSignature = rootSignature.get();
		desc.cs = shaderCS;

		pipelineState = std::unique_ptr<PipelineState>(gRenderDevice->createComputePipelineState(desc));
	}

	//////////////////////////////////////////////////////////////////////////
	// Bindless materials

	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	materialCBVMemory = std::unique_ptr<ConstantBuffer>(
		gRenderDevice->createConstantBuffer(MATERIAL_MEMORY_POOL_SIZE));

	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::CBV;
		desc.numDescriptors = MAX_MATERIAL_CBVs * swapchainCount;
		desc.flags = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		materialCBVHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::SRV;
		desc.numDescriptors = MAX_MATERIAL_SRVs * swapchainCount;
		desc.flags = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		materialSRVHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}

	materialCBVs.resize(MAX_MATERIAL_CBVs);
	for (size_t i = 0; i < materialCBVs.size(); ++i)
	{
		materialCBVs[i] = std::unique_ptr<ConstantBufferView>(
			materialCBVMemory->allocateCBV(materialCBVHeap.get(), sizeof(MaterialConstants), swapchainCount));
	}
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
			sceneData[k].modelTransform = sm->getTransform().getMatrix();
			// #todo: uint64 offset
			sceneData[k].positionBufferOffset    = (uint32)section.positionBuffer->getBufferOffsetInBytes();
			sceneData[k].nonPositionBufferOffset = (uint32)section.nonPositionBuffer->getBufferOffsetInBytes();
			sceneData[k].indexBufferOffset       = (uint32)section.indexBuffer->getBufferOffsetInBytes();
			++k;
		}
	}
	gpuSceneBuffer->uploadData(
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

	commandList->setComputeRootConstant32(0, numMeshSections, 0);
	commandList->setComputeRootDescriptorUAV(1, gpuSceneBuffer->getUAV());
	commandList->setComputeRootDescriptorUAV(2, culledGpuSceneBuffer->getUAV());

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

StructuredBuffer* GPUScene::getGPUSceneBuffer() const
{
	return gpuSceneBuffer.get();
}

StructuredBuffer* GPUScene::getCulledGPUSceneBuffer() const
{
	return culledGpuSceneBuffer.get();
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
