#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "util/volatile_descriptor.h"

struct CombineLightingPassInput
{
	ConstantBufferView* sceneUniformCBV;
	Texture*            sceneColorTexture;
	RenderTargetView*   sceneColorRTV;
	Texture*            sceneDepthTexture;
	ShaderResourceView* sceneDepthSRV;
	Texture*            gbuffer0Texture;
	ShaderResourceView* gbuffer0SRV;
	Texture*            gbuffer1Texture;
	ShaderResourceView* gbuffer1SRV;
	Texture*            indirectDiffuseTexture;
	ShaderResourceView* indirectDiffuseSRV;
	Texture*            indirectSpecularTexture;
	ShaderResourceView* indirectSpecularSRV;
};

class CombineLightingPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice, EPixelFormat PF_sceneColor);

	void combineLighting(RenderCommandList* commandList, const FrameInfo& frameInfo, const CombineLightingPassInput& passInput);

private:
	UniquePtr<GraphicsPipelineState> pipelineState;
	VolatileDescriptorHelper         passDescriptor;
};
