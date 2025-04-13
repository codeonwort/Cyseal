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

// #todo-basepass: kMaxBasePassPermutation
#define kMaxBasePassPermutation 2

class MaterialAsset;
class SceneProxy;
class Camera;
class GPUScene;
class GPUCulling;
struct StaticMeshSection;

// -----------------------------------------
// PSO permutation

using GraphicsPipelineKey = uint32;

struct IndirectDrawHelper
{
	IndirectDrawHelper(GraphicsPipelineKey inPipelineKey)
		: pipelineKey(inPipelineKey)
	{
	}

	void resizeResources(uint32 swapchainIndex, uint32 maxDrawCount);

	GraphicsPipelineKey pipelineKey;

	UniquePtr<CommandSignature> commandSignature;
	UniquePtr<IndirectCommandGenerator> argumentBufferGenerator;

	BufferedUniquePtr<Buffer> argumentBuffer;
	BufferedUniquePtr<Buffer> culledArgumentBuffer;
	BufferedUniquePtr<Buffer> drawCounterBuffer;

	BufferedUniquePtr<ShaderResourceView> argumentBufferSRV;
	BufferedUniquePtr<UnorderedAccessView> culledArgumentBufferUAV;
	BufferedUniquePtr<UnorderedAccessView> drawCounterBufferUAV;
};

// Can't think of better name
struct GraphicsPipelineItem
{
	GraphicsPipelineState* pipelineState;
	IndirectDrawHelper* indirectDrawHelper;
};

class GraphicsPipelineStatePermutation
{
public:
	~GraphicsPipelineStatePermutation();

	GraphicsPipelineItem findPipeline(GraphicsPipelineKey key) const;

	void insertPipeline(GraphicsPipelineKey key, GraphicsPipelineItem item);

private:
	std::map<GraphicsPipelineKey, GraphicsPipelineItem> pipelines;
};

struct GraphicsPipelineKeyDesc
{
	ECullMode cullMode;
};

// -----------------------------------------
// BasePass

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

// #todo-basepass: Temp struct for pipeline permutation support
struct BasePassDrawList
{
	std::vector<const StaticMeshSection*> meshes;
	std::vector<uint32> objectIDs;

	void reserve(size_t n)
	{
		meshes.reserve(n);
		objectIDs.reserve(n);
	}
};

// Render direct lighting + gbuffers.
class BasePass final : public SceneRenderPass
{
public:
	~BasePass();

	void initialize(EPixelFormat sceneColorFormat, const EPixelFormat gbufferForamts[], uint32 numGBuffers);

	void renderBasePass(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput);

private:
	void renderForPipeline(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput, GraphicsPipelineKey pipelineKey, const BasePassDrawList& drawList);

	GraphicsPipelineState* createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc);
	IndirectDrawHelper* createIndirectDrawHelper(GraphicsPipelineState* pipelineState, GraphicsPipelineKey pipelineKey);
	void resizeVolatileHeaps(uint32 swapchainIndex, uint32 maxDescriptors);

private:
	GraphicsPipelineStatePermutation pipelinePermutation;
	EPixelFormat sceneColorFormat;
	std::vector<EPixelFormat> gbufferFormats;
	ShaderStage* shaderVS = nullptr;
	ShaderStage* shaderPS = nullptr;

	std::vector<uint32> totalVolatileDescriptor;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
