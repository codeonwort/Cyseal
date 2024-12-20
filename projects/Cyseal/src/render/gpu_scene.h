#pragma once

#include "core/vec3.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"

class SceneProxy;
class Camera;

struct MaterialConstants
{
	vec3 albedoMultiplier = vec3(1.0f, 1.0f, 1.0f);
	float roughness = 0.0f;

	uint32 albedoTextureIndex;
	vec3 emission = vec3(0.0f, 0.0f, 0.0f);
};

class GPUScene final
{
public:
	struct MaterialDescriptorsDesc
	{
		DescriptorHeap* cbvHeap;
		DescriptorHeap* srvHeap;
		uint32 cbvCount;
		uint32 srvCount;
	};

public:
	void initialize();

	// Update GPU scene buffer.
	void renderGPUScene(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		const SceneProxy* scene,
		const Camera* camera,
		ConstantBufferView* sceneUniform,
		bool bRenderAnyRaytracingPass);

	ShaderResourceView* getGPUSceneBufferSRV() const;

	MaterialDescriptorsDesc queryMaterialDescriptors(uint32 swapchainIndex) const;

	// Query how many descriptors are needed.
	void queryMaterialDescriptorsCount(uint32 swapchainIndex, uint32& outCBVCount, uint32& outSRVCount);

	inline uint32 getGPUSceneItemMaxCount() const { return gpuSceneMaxElements; }

private:
	void resizeVolatileHeaps(uint32 swapchainIndex, uint32 maxDescriptors);
	void resizeGPUSceneCommandBuffer(uint32 swapchainIndex, uint32 maxElements);
	void resizeGPUSceneBuffer(RenderCommandList* commandList, uint32 maxElements);
	void resizeMaterialBuffers(uint32 swapchainIndex, uint32 maxCBVCount, uint32 maxSRVCount);

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
	std::vector<uint32> materialCBVMaxCounts;
	std::vector<uint32> materialSRVMaxCounts;
	std::vector<uint32> materialCBVActualCounts; // Currently same as max count.
	std::vector<uint32> materialSRVActualCounts; // Currently same as max count.
	BufferedUniquePtr<Buffer> materialCBVMemory;
	BufferedUniquePtr<DescriptorHeap> materialCBVHeap;
	BufferedUniquePtr<DescriptorHeap> materialSRVHeap;
	BufferedUniquePtrVec<ConstantBufferView> materialCBVs;
	BufferedUniquePtrVec<ShaderResourceView> materialSRVs;
};
