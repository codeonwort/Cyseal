#pragma once

#include "core/vec3.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"

class SceneProxy;
class Camera;

// Should match with Material in material.hlsl.
struct MaterialConstants
{
	vec3   albedoMultiplier   = vec3(1.0f, 1.0f, 1.0f);
	float  roughness          = 0.0f;

	uint32 albedoTextureIndex = 0xffffffff;
	vec3   emission           = vec3(0.0f, 0.0f, 0.0f);

	float  metalMask          = 0.0f;
	uint32 _pad0;
	uint32 _pad1;
	uint32 _pad2;
};

struct GPUSceneInput
{
	const SceneProxy*   scene;
	const Camera*       camera;
	ConstantBufferView* sceneUniform;
	bool                bRenderAnyRaytracingPass;
};

class GPUScene final
{
public:
	struct MaterialDescriptorsDesc
	{
		ShaderResourceView* constantsBufferSRV; // Structured buffer that contains all material constants per mesh section.
		DescriptorHeap* srvHeap; // Descriptor heap that contains all material textures.
		uint32 srvCount; // Total material texture count.
	};

public:
	void initialize();

	// Update GPU scene buffer.
	void renderGPUScene(RenderCommandList* commandList, uint32 swapchainIndex, const GPUSceneInput& passInput);

	ShaderResourceView* getGPUSceneBufferSRV() const;

	MaterialDescriptorsDesc queryMaterialDescriptors(uint32 swapchainIndex) const;

	inline uint32 getGPUSceneItemMaxCount() const { return gpuSceneMaxElements; }

private:
	void resizeVolatileHeaps(uint32 swapchainIndex, uint32 maxDescriptors);
	void resizeGPUSceneCommandBuffer(uint32 swapchainIndex, uint32 maxElements);
	void resizeGPUSceneBuffer(RenderCommandList* commandList, uint32 maxElements);
	void resizeMaterialBuffers(uint32 swapchainIndex, uint32 maxConstantsCount, uint32 maxSRVCount);

private:
	UniquePtr<ComputePipelineState> pipelineState;

	std::vector<uint32> totalVolatileDescriptors;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;

	// GPU scene command buffers (per swapchain)
	std::vector<uint32> gpuSceneCommandBufferMaxElements;
	BufferedUniquePtr<Buffer> gpuSceneCommandBuffer;
	BufferedUniquePtr<ShaderResourceView> gpuSceneCommandBufferSRV;

	// GPU scene buffer (NOT per swapchain)
	uint32 gpuSceneMaxElements = 0;
	UniquePtr<Buffer> gpuSceneBuffer;
	UniquePtr<ShaderResourceView> gpuSceneBufferSRV;
	UniquePtr<UnorderedAccessView> gpuSceneBufferUAV;

	// Bindless materials (per swapchain)
	// #todo-gpuscene: Maybe I don't need to separate max count and actual count?
	// Currently constants count = srv count as there are only albedo textures, but srv count will increase.
	std::vector<uint32> materialConstantsMaxCounts;
	std::vector<uint32> materialSRVMaxCounts;
	std::vector<uint32> materialConstantsActualCounts; // Currently same as max count.
	std::vector<uint32> materialSRVActualCounts; // Currently same as max count.
	BufferedUniquePtr<DescriptorHeap> materialSRVHeap;
	BufferedUniquePtrVec<ShaderResourceView> materialSRVs;

	BufferedUniquePtr<Buffer> materialConstantsMemory;
	BufferedUniquePtr<DescriptorHeap> materialConstantsHeap;
	BufferedUniquePtr<ShaderResourceView> materialConstantsSRV;
};
