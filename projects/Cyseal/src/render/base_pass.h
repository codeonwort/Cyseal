#pragma once

#include "scene_render_pass.h"
#include "static_mesh_rendering.h"
#include "rhi/rhi_forward.h"
#include "core/smart_pointer.h"
#include "util/volatile_descriptor.h"

// #todo-basepass: kMaxBasePassPermutation
#define kMaxBasePassPermutation 2

class SceneProxy;
class Camera;
class GPUScene;
class GPUCulling;

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

	void initialize(RenderDevice* inRenderDevice, EPixelFormat sceneColorFormat, const EPixelFormat gbufferForamts[], uint32 numGBuffers, EPixelFormat velocityMapFormat);

	void renderBasePass(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput);

private:
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
