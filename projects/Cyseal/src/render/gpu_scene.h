#pragma once

#include "core/vec3.h"
#include "rhi/gpu_resource_view.h"

#include <vector>
#include <memory>

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
	vec3 _pad0;
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

	// Query how many descriptors are needed.
	// Use this before copyMaterialDescriptors() if you're unsure the dest heap is big enough.
	void queryMaterialDescriptorsCount(uint32& outCBVCount, uint32& outSRVCount);

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
	void resizeVolatileHeaps(uint32 maxDescriptors);
	void resizeGPUSceneCommandBuffer(uint32 swapchainIndex, uint32 maxElements);
	void resizeGPUSceneBuffers(uint32 maxElements);
	void resizeMaterialBuffers(uint32 maxCBVCount, uint32 maxSRVCount);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;

	uint32 totalVolatileDescriptors = 0;
	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;

	// GPU scene command buffer
	std::vector<uint32> gpuSceneCommandBufferMaxElements;
	std::vector<std::unique_ptr<Buffer>> gpuSceneCommandBuffers;
	std::vector<std::unique_ptr<ShaderResourceView>> gpuSceneCommandBufferSRVs;

	// GPU scene buffer
	uint32 gpuSceneMaxElements = 0;
	std::unique_ptr<Buffer> gpuSceneBuffer;
	std::unique_ptr<ShaderResourceView> gpuSceneBufferSRV;
	std::unique_ptr<UnorderedAccessView> gpuSceneBufferUAV;

	// Bindless materials
	std::unique_ptr<Buffer> materialCBVMemory;
	std::unique_ptr<DescriptorHeap> materialCBVHeap;
	std::unique_ptr<DescriptorHeap> materialSRVHeap;
	std::vector<std::vector<std::unique_ptr<ConstantBufferView>>> materialCBVsPerFrame;
	uint32 currentMaterialCBVCount = 0;
	uint32 currentMaterialSRVCount = 0;
};
