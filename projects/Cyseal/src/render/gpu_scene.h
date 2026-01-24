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
};

class GPUScene final : public SceneRenderPass
{
public:
	struct MaterialDescriptorsDesc
	{
		ShaderResourceView* constantsBufferSRV; // Structured buffer that contains all material constants per mesh section.
		DescriptorHeap*     srvHeap;            // Descriptor heap that contains all material textures.
		uint32              srvCount;           // Total material texture count.
	};

public:
	void initialize(RenderDevice* renderDevice);

	// Update GPU scene buffer.
	void renderGPUScene(RenderCommandList* commandList, uint32 swapchainIndex, const GPUSceneInput& passInput);

	// Might return null if no gpu scene item was allocated yet.
	ShaderResourceView* getGPUSceneBufferSRV() const;

	MaterialDescriptorsDesc queryMaterialDescriptors() const;

	inline uint32 getGPUSceneItemMaxCount() const { return gpuSceneMaxElements; }

private:
	void resizeGPUSceneBuffer(RenderCommandList* commandList, uint32 maxElements);
	void resizeGPUSceneCommandBuffers(uint32 swapchainIndex, const SceneProxy* scene);
	void executeGPUSceneCommands(RenderCommandList* commandList, uint32 swapchainIndex, const SceneProxy* scene);

	void resizeMaterialBuffer(RenderCommandList* commandList, uint32 maxElements);
	void resizeBindlessTextures(RenderCommandList* commandList, uint32 maxElements);
	void resizeMaterialCommandBuffer(uint32 swapchainIndex, const SceneProxy* scene);
	void executeMaterialCommands(RenderCommandList* commandList, uint32 swapchainIndex, const SceneProxy* scene);

private:
	RenderDevice* device = nullptr;

	// ----------------------------------------------
	// GPU scene buffer

	uint32                                gpuSceneMaxElements = 0;
	UniquePtr<Buffer>                     gpuSceneBuffer;
	UniquePtr<ShaderResourceView>         gpuSceneBufferSRV;
	UniquePtr<UnorderedAccessView>        gpuSceneBufferUAV;

	// ----------------------------------------------
	// GPU scene commands

	UniquePtr<ComputePipelineState>       evictPipelineState;
	UniquePtr<ComputePipelineState>       allocPipelineState;
	UniquePtr<ComputePipelineState>       updatePipelineState;
	VolatileDescriptorHelper              passDescriptor; // For all 3 pipelines above

	BufferedUniquePtr<Buffer>             gpuSceneEvictCommandBuffer;
	BufferedUniquePtr<Buffer>             gpuSceneAllocCommandBuffer;
	BufferedUniquePtr<Buffer>             gpuSceneUpdateCommandBuffer;
	BufferedUniquePtr<ShaderResourceView> gpuSceneEvictCommandBufferSRV;
	BufferedUniquePtr<ShaderResourceView> gpuSceneAllocCommandBufferSRV;
	BufferedUniquePtr<ShaderResourceView> gpuSceneUpdateCommandBufferSRV;

	// ----------------------------------------------
	// Bindless materials
	using UniqueSrvVec = std::vector<UniquePtr<ShaderResourceView>>;

	uint32                                materialBufferMaxElements = 0;
	UniquePtr<Buffer>                     materialConstantsBuffer;
	UniquePtr<ShaderResourceView>         materialConstantsSRV;
	UniquePtr<UnorderedAccessView>        materialConstantsUAV;

	UniquePtr<DescriptorHeap>             bindlessTextureHeap;
	UniqueSrvVec                          bindlessSRVs;

	// ----------------------------------------------
	// Bindless material commands

	UniquePtr<ComputePipelineState>       materialPipelineState;
	VolatileDescriptorHelper              materialPassDescriptor;

	BufferedUniquePtr<Buffer>             materialCommandBuffer;
	BufferedUniquePtr<ShaderResourceView> materialCommandSRV;
};
