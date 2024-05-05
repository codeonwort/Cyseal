#pragma once

#include "core/vec3.h"
#include "core/smart_pointer.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/gpu_resource_binding.h"

class RenderCommandList;
class PipelineState;
class RootSignature;
class DescriptorHeap;
class Buffer;
class ConstantBufferView;
class ShaderResourceView;
class UnorderedAccessView;
class SceneProxy;
class Camera;

struct MaterialConstants
{
	float albedoMultiplier[3] = { 1.0f, 1.0f, 1.0f };
	float roughness = 0.0f;

	uint32 albedoTextureIndex;
	vec3 emission;
};

class GPUScene final
{
	friend class GPUCulling;

public:
	void initialize();

	// Update GPU scene buffer.
	void renderGPUScene(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		const SceneProxy* scene,
		const Camera* camera,
		ConstantBufferView* sceneUniform);

	ShaderResourceView* getGPUSceneBufferSRV() const;

	// GPU scene buffer won't be modified from outside, so exposing only SRV is OK.
	//UnorderedAccessView* getGPUSceneBufferUAV() const;

	// Query how many descriptors are needed.
	// Use this before copyMaterialDescriptors() if you're unsure the dest heap is big enough.
	void queryMaterialDescriptorsCount(uint32 swapchainIndex, uint32& outCBVCount, uint32& outSRVCount);

	// Copy material CBV/SRV descriptors to 'destHeap', starting from its 'destBaseIndex'.
	// This method will copy a variable number of descriptors, so other descriptors
	// unrelated to material descriptors can be bound starting from 'outNextAvailableIndex'.
	void copyMaterialDescriptors(
		uint32 swapchainIndex,
		DescriptorHeap* destHeap, uint32 destBaseIndex,
		uint32& outCBVBaseIndex, uint32& outCBVCount,
		uint32& outSRVBaseIndex, uint32& outSRVCount,
		uint32& outNextAvailableIndex);

	inline uint32 getGPUSceneItemMaxCount() const { return gpuSceneMaxElements; }

private:
	void resizeVolatileHeaps(uint32 swapchainIndex, uint32 maxDescriptors);
	void resizeGPUSceneCommandBuffer(uint32 swapchainIndex, uint32 maxElements);
	void resizeGPUSceneBuffer(RenderCommandList* commandList, uint32 maxElements);
	void resizeMaterialBuffers(uint32 swapchainIndex, uint32 maxCBVCount, uint32 maxSRVCount);

private:
	UniquePtr<PipelineState> pipelineState;
	UniquePtr<RootSignature> rootSignature;

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
