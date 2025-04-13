#pragma once

#include "scene_render_pass.h"
#include "core/vec3.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/descriptor_heap.h"

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
	void initialize(uint32 inMaxBasePassPermutation);

	void resetCullingResources();

	void cullDrawCommands(RenderCommandList* commandList, uint32 swapchainIndex, const GPUCullingInput& passInput);

private:
	void resizeVolatileHeap(uint32 swapchainIndex, uint32 maxDescriptors);

private:
	UniquePtr<ComputePipelineState> pipelineState;

	std::vector<uint32> totalVolatileDescriptor;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
	DescriptorIndexTracker descriptorIndexTracker;
	uint32 maxBasePassPermutation = 1;
};
