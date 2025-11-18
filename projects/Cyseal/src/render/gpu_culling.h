#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/descriptor_heap.h"
#include "util/volatile_descriptor.h"

class GPUScene;
class SceneProxy;
class Camera;

struct GPUCullingInput
{
	const Camera*           camera;
	ConstantBufferView*     sceneUniform;
	GPUScene*               gpuScene;
	uint32                  maxDrawCommands;
	Buffer*                 indirectDrawBuffer;
	Buffer*                 culledIndirectDrawBuffer;
	Buffer*                 drawCounterBuffer;
	ShaderResourceView*     indirectDrawBufferSRV;
	UnorderedAccessView*    culledIndirectDrawBufferUAV;
	UnorderedAccessView*    drawCounterBufferUAV;
};

// Cull indirect draw commands using GPU scene.
class GPUCulling final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice, uint32 inMaxBasePassPermutation);

	void resetCullingResources();

	void cullDrawCommands(RenderCommandList* commandList, uint32 swapchainIndex, const GPUCullingInput& passInput);

private:
	UniquePtr<ComputePipelineState> pipelineState;
	VolatileDescriptorHelper        passDescriptor;
	DescriptorIndexTracker          descriptorIndexTracker;
	uint32                          maxBasePassPermutation = 1;
};
