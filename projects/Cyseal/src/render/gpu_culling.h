#pragma once

#include "core/vec3.h"
#include "rhi/gpu_resource_view.h"

#include <vector>
#include <memory>

class GPUScene;

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

// Cull indirect draw commands using GPU scene.
class GPUCulling final
{
public:
	void initialize();

	void cullDrawCommands(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		ConstantBufferView* sceneUniform,
		const Camera* camera,
		GPUScene* gpuScene,
		uint32 maxDrawCommands,
		Buffer* indirectDrawBuffer,
		ShaderResourceView* indirectDrawBufferSRV,
		Buffer* culledIndirectDrawBuffer,
		UnorderedAccessView* culledIndirectDrawBufferUAV,
		Buffer* drawCounterBuffer,
		UnorderedAccessView* drawCounterBufferUAV);

private:
	void resizeVolatileHeaps(uint32 maxDescriptors);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;

	uint32 totalVolatileDescriptors = 0;
	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
};
