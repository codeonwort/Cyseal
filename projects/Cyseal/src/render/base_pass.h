#pragma once

#include "scene_render_pass.h"
#include "static_mesh_rendering.h"
#include "renderer_options.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"
#include "util/volatile_descriptor.h"

#include <map>

// #todo-basepass: kMaxBasePassPermutation
#define kMaxBasePassPermutation 2

class MaterialAsset;
class SceneProxy;
class Camera;
class GPUScene;
class GPUCulling;
struct StaticMeshSection;

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

// #wip: Generalize a bit and move to static_mesh_rendering.h
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

	void initialize(RenderDevice* inRenderDevice, EPixelFormat sceneColorFormat, const EPixelFormat gbufferForamts[], uint32 numGBuffers, EPixelFormat velocityMapFormat);

	void renderBasePass(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput);

private:
	void renderForPipeline(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput, GraphicsPipelineKey pipelineKey, const BasePassDrawList& drawList);

	GraphicsPipelineState* createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc);

private:
	RenderDevice*                    device = nullptr;
	GraphicsPipelineStatePermutation pipelinePermutation;
	EPixelFormat                     sceneColorFormat;
	std::vector<EPixelFormat>        gbufferFormats;
	EPixelFormat                     velocityMapFormat;
	ShaderStage*                     shaderVS = nullptr;
	ShaderStage*                     shaderPS = nullptr;
	VolatileDescriptorHelper         passDescriptor;
};
