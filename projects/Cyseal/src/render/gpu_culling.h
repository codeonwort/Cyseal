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
	// #wip: Only need camera frustum (4 * 6 floats); replace with root constants?
	// Also it can be acquired by just calling camera->getFrustum().
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

	// Invoke every frame before calling cullDrawCommands().
	void resetCullingResources();

	// Can be invoked multiple times within a frame.
	// Draw commands are accumulated from the start of passInput.culledIndirectDrawBuffer,
	// so you need to provide different culledIndirectDrawBuffer and drawCounterBuffer in passInput for each invocation.
	void cullDrawCommands(RenderCommandList* commandList, uint32 swapchainIndex, const GPUCullingInput& passInput);

private:
	UniquePtr<ComputePipelineState> pipelineState;
	VolatileDescriptorHelper        passDescriptor;
	DescriptorIndexTracker          descriptorIndexTracker;
	uint32                          maxBasePassPermutation = 1;
};
