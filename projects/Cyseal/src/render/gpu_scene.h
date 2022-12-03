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
class ConstantBuffer;
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
public:
	void initialize();
	void renderGPUScene(RenderCommandList* commandList, const SceneProxy* scene, const Camera* camera);

	ShaderResourceView* getGPUSceneBufferSRV() const;
	ShaderResourceView* getCulledGPUSceneBufferSRV() const;

	// Query how many descriptors are needed.
	// Use this before copyMaterialDescriptors() if you're unsure the dest heap is big enough.
	void queryMaterialDescriptorsCount(uint32& outCBVCount, uint32& outSRVCount);

	// Copy material CBV/SRV descriptors to 'destHeap', starting from its 'destBaseIndex'.
	// This method will copy a variable number of descriptors, so other descriptors
	// unrelated to material descriptors can be bound starting from 'outNextAvailableIndex'.
	void copyMaterialDescriptors(
		DescriptorHeap* destHeap, uint32 destBaseIndex,
		uint32& outCBVBaseIndex, uint32& outCBVCount,
		uint32& outSRVBaseIndex, uint32& outSRVCount,
		uint32& outNextAvailableIndex);

private:
	void resizeGPUSceneBuffers(uint32 maxElements);
	void resizeMaterialBuffers(uint32 maxCBVCount, uint32 maxSRVCount);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;

	// GPU scene buffers
	uint32 gpuSceneMaxElements = 0;
	std::unique_ptr<Buffer> gpuSceneBuffer;
	std::unique_ptr<Buffer> culledGpuSceneBuffer;

	// GPU scene buffer views
	std::unique_ptr<ShaderResourceView> gpuSceneBufferSRV;
	std::unique_ptr<ShaderResourceView> culledGpuSceneBufferSRV;
	std::unique_ptr<UnorderedAccessView> gpuSceneBufferUAV;
	std::unique_ptr<UnorderedAccessView> culledGpuSceneBufferUAV;

	// Bindless materials
	std::unique_ptr<Buffer> materialCBVMemory;
	std::unique_ptr<DescriptorHeap> materialCBVHeap;
	std::unique_ptr<DescriptorHeap> materialSRVHeap;
	std::vector<std::vector<std::unique_ptr<ConstantBufferView>>> materialCBVsPerFrame;
	uint32 currentMaterialCBVCount = 0;
	uint32 currentMaterialSRVCount = 0;
};
