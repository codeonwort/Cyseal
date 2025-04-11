#pragma once

#include "scene_render_pass.h"
#include "renderer_options.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"

#include <map>

class MaterialAsset;
class SceneProxy;
class Camera;
class GPUScene;
class GPUCulling;

using GraphicsPipelineKey = uint32;

class GraphicsPipelineStatePermutation
{
public:
	~GraphicsPipelineStatePermutation();

	GraphicsPipelineState* find(GraphicsPipelineKey key) const;

	void insert(GraphicsPipelineKey key, GraphicsPipelineState* pipeline);

private:
	std::map<GraphicsPipelineKey, GraphicsPipelineState*> permutations;
};

struct GraphicsPipelineKeyDesc
{
	ECullMode cullMode;
};

struct BasePassInput
{
	const SceneProxy*      scene;
	const Camera*          camera;
	bool                   bIndirectDraw;
	bool                   bGPUCulling;

	ConstantBufferView*    sceneUniformBuffer;
	GPUScene*              gpuScene;
	GPUCulling*            gpuCulling;
	ShaderResourceView*    shadowMaskSRV;
};

// Render direct lighting + gbuffers.
class BasePass final : public SceneRenderPass
{
public:
	~BasePass();

	void initialize(EPixelFormat sceneColorFormat, const EPixelFormat gbufferForamts[], uint32 numGBuffers);

	void renderBasePass(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput);

private:
	GraphicsPipelineState* createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc);
	void resizeVolatileHeaps(uint32 swapchainIndex, uint32 maxDescriptors);

private:
	GraphicsPipelineStatePermutation pipelinePermutation;
	EPixelFormat sceneColorFormat;
	std::vector<EPixelFormat> gbufferFormats;
	ShaderStage* shaderVS = nullptr;
	ShaderStage* shaderPS = nullptr;

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
};
