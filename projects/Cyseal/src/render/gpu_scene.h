#pragma once

#include "scene_render_pass.h"
#include "core/vec3.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"
#include "util/volatile_descriptor.h"

class SceneProxy;
class Camera;

struct GPUSceneInput
{
	const SceneProxy*   scene;
	const Camera*       camera;
	ConstantBufferView* sceneUniform;
	bool                bRenderAnyRaytracingPass;
};

class GPUScene final : public SceneRenderPass
{
public:
	struct MaterialDescriptorsDesc
	{
		ShaderResourceView* constantsBufferSRV; // Structured buffer that contains all material constants per mesh section.
		DescriptorHeap* srvHeap; // Descriptor heap that contains all material textures.
		uint32 srvCount; // Total material texture count.
	};

public:
	void initialize(RenderDevice* renderDevice);

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

	void resizeGPUSceneCommandBuffers(RenderCommandList* commandList, uint32 swapchainIndex, const SceneProxy* scene);
	void executeGPUSceneCommands(RenderCommandList* commandList, uint32 swapchainIndex, const SceneProxy* scene);

private:
	RenderDevice* device = nullptr;

	// #wip: Delete old impl
	UniquePtr<ComputePipelineState> pipelineState;
	std::vector<uint32> totalVolatileDescriptors;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
	// GPU scene command buffers (per swapchain)
	std::vector<uint32> gpuSceneCommandBufferMaxElements;
	BufferedUniquePtr<Buffer> gpuSceneCommandBuffer;
	BufferedUniquePtr<ShaderResourceView> gpuSceneCommandBufferSRV;

	UniquePtr<ComputePipelineState> evictPipelineState;
	UniquePtr<ComputePipelineState> allocPipelineState;
	UniquePtr<ComputePipelineState> updatePipelineState;
	VolatileDescriptorHelper passDescriptor; // For all 3 pipelines above

	BufferedUniquePtr<Buffer> gpuSceneEvictCommandBuffer;
	BufferedUniquePtr<Buffer> gpuSceneAllocCommandBuffer;
	BufferedUniquePtr<Buffer> gpuSceneUpdateCommandBuffer;
	BufferedUniquePtr<ShaderResourceView> gpuSceneEvictCommandBufferSRV;
	BufferedUniquePtr<ShaderResourceView> gpuSceneAllocCommandBufferSRV;
	BufferedUniquePtr<ShaderResourceView> gpuSceneUpdateCommandBufferSRV;

	// GPU scene buffer (NOT per swapchain)
	uint32 gpuSceneMaxElements = 0;
	UniquePtr<Buffer> gpuSceneBuffer;
	UniquePtr<ShaderResourceView> gpuSceneBufferSRV;
	UniquePtr<UnorderedAccessView> gpuSceneBufferUAV;

	// Bindless materials (per swapchain)
	// #wip-material: Maybe I don't need to separate max count and actual count?
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
