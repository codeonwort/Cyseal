#pragma once

#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"
#include <memory>

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
		ConstantBufferView* sceneUniformBuffer,
		GPUScene* gpuScene,
		GPUCulling* gpuCulling,
		Texture* RT_sceneColor,
		Texture* RT_thinGBufferA);

private:
	// Bind root parameters for the current root signature
	void bindRootParameters(
		RenderCommandList* cmdList,
		ConstantBufferView* sceneUniform,
		GPUScene* gpuScene);

	void resizeVolatileHeaps(uint32 maxDescriptors);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;

	std::unique_ptr<CommandSignature> commandSignature;
	std::unique_ptr<IndirectCommandGenerator> argumentBufferGenerator;
	std::vector<std::unique_ptr<Buffer>> argumentBuffers;
	std::vector<std::unique_ptr<ShaderResourceView>> argumentBufferSRVs;
	std::vector<std::unique_ptr<Buffer>> culledArgumentBuffers;
	std::vector<std::unique_ptr<UnorderedAccessView>> culledArgumentBufferUAVs;
	std::vector<std::unique_ptr<Buffer>> drawCounterBuffers;
	std::vector<std::unique_ptr<UnorderedAccessView>> drawCounterBufferUAVs;

	uint32 totalVolatileDescriptors = 0;
	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
	// #todo-sampler: Maybe need a volatileSamplerHeap in similar way?
};
