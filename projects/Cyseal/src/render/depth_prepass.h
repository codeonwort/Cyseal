#pragma once

#include "scene_render_pass.h"
#include "static_mesh_rendering.h"
#include "rhi/rhi_forward.h"
#include "core/smart_pointer.h"
#include "util/volatile_descriptor.h"

class SceneProxy;
class Camera;
class GPUScene;
class GPUCulling;

struct DepthPrepassInput
{
	const SceneProxy*      scene;
	const Camera*          camera;
	bool                   bIndirectDraw;
	bool                   bGPUCulling;
	bool                   bVisibilityBuffer;

	ConstantBufferView*    sceneUniformBuffer;
	GPUScene*              gpuScene;
	GPUCulling*            gpuCulling;
};

// Render scene dpeth.
class DepthPrepass final : public SceneRenderPass
{
public:
	~DepthPrepass();

	void initialize(RenderDevice* inRenderDevice);

	void renderDepthPrepass(RenderCommandList* commandList, uint32 swapchainIndex, const DepthPrepassInput& passInput);

private:
	GraphicsPipelineState* createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc);

private:
	RenderDevice*                    device = nullptr;
	GraphicsPipelineStatePermutation pipelinePermutation;
	ShaderStage*                     shaderVS = nullptr;
	ShaderStage*                     shaderPS = nullptr;
	VolatileDescriptorHelper         passDescriptor;
};
