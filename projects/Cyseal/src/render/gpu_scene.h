#pragma once

#include "core/vec3.h"
#include "gpu_resource_view.h"

#include <vector>
#include <memory>

class PipelineState;
class RootSignature;
class StructuredBuffer;
class RenderCommandList;
class SceneProxy;
class Camera;
class DescriptorHeap;
class ConstantBuffer;

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

	StructuredBuffer* getGPUSceneBuffer() const;
	StructuredBuffer* getCulledGPUSceneBuffer() const;

	// Copy material CBV/SRV descriptors to 'destHeap', starting from its 'destBaseIndex'.
	// This method will copy a variable number of descriptors, so other descriptors
	// unrelated to material descriptors can be bound starting from 'outNextAvailableIndex'.
	void copyMaterialDescriptors(
		DescriptorHeap* destHeap, uint32 destBaseIndex,
		uint32& outCBVBaseIndex, uint32& outCBVCount,
		uint32& outSRVBaseIndex, uint32& outSRVCount,
		uint32& outNextAvailableIndex);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;

	std::unique_ptr<StructuredBuffer> gpuSceneBuffer;
	std::unique_ptr<StructuredBuffer> culledGpuSceneBuffer;

	std::unique_ptr<ConstantBuffer> materialCBVMemory;
	std::unique_ptr<DescriptorHeap> materialCBVHeap;
	std::unique_ptr<DescriptorHeap> materialSRVHeap;
	std::vector<std::unique_ptr<ConstantBufferView>> materialCBVs;
	uint32 currentMaterialCBVCount = 0;
	uint32 currentMaterialSRVCount = 0;
};
