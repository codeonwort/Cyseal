#pragma once

#include "renderer.h"
#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"

class RenderCommandList;
class Material;
class SceneProxy;
class Camera;
class GPUScene;
class GPUCulling;
class Texture;

class BasePass final
{
public:
	void initialize();

	void renderBasePass(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		const SceneProxy* scene,
		const Camera* camera,
		const RendererOptions& rendererOptions,
		ConstantBufferView* sceneUniformBuffer,
		GPUScene* gpuScene,
		GPUCulling* gpuCulling,
		Texture* RT_sceneColor,
		Texture* RT_thinGBufferA);

private:
	// Bind root parameters for the current root signature
	void bindRootParameters(
		RenderCommandList* cmdList,
		uint32 swapchainIndex,
		ConstantBufferView* sceneUniform,
		GPUScene* gpuScene);

	void resizeVolatileHeaps(uint32 swapchainIndex, uint32 maxDescriptors);

private:
	UniquePtr<GraphicsPipelineState> pipelineState;
	UniquePtr<RootSignature> rootSignature;

	UniquePtr<CommandSignature> commandSignature;
	UniquePtr<IndirectCommandGenerator> argumentBufferGenerator;

	BufferedUniquePtr<Buffer> argumentBuffer;
	BufferedUniquePtr<ShaderResourceView> argumentBufferSRV;
	BufferedUniquePtr<Buffer> culledArgumentBuffer;
	BufferedUniquePtr<UnorderedAccessView> culledArgumentBufferUAV;
	BufferedUniquePtr<Buffer> drawCounterBuffer;
	BufferedUniquePtr<UnorderedAccessView> drawCounterBufferUAV;

	std::vector<uint32> totalVolatileDescriptor;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
	// #todo-sampler: Maybe need a volatileSamplerHeap in similar way?
};
