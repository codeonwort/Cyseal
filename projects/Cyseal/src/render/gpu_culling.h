#pragma once

#include "core/vec3.h"
#include "core/smart_pointer.h"
#include "rhi/gpu_resource_view.h"

class GPUScene;

class RenderCommandList;
class ShaderStage;
class ComputePipelineState;
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
	void resizeVolatileHeap(uint32 swapchainIndex, uint32 maxDescriptors);

private:
	UniquePtr<ShaderStage> gpuCullingShader;
	UniquePtr<ComputePipelineState> pipelineState;

	std::vector<uint32> totalVolatileDescriptor;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
