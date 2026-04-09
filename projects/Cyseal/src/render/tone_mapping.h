#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include "util/volatile_descriptor.h"

class RenderCommandList;
class ShaderResourceView;

struct ToneMappingInput
{
	Texture*            renderTarget; // If null we're rendering to backbuffer.
	Viewport            viewport;
	ScissorRect         scissorRect;
	ConstantBufferView* sceneUniformCBV;
	ShaderResourceView* sceneColorSRV;
	ShaderResourceView* sceneDepthSRV;
	ShaderResourceView* gbuffer0SRV;
	ShaderResourceView* gbuffer1SRV;
	ShaderResourceView* indirectDiffuseSRV;
	ShaderResourceView* indirectSpecularSRV;
};

class ToneMapping final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void renderToneMapping(RenderCommandList* commandList, uint32 swapchainIndex, const ToneMappingInput& passInput);

private:
	GraphicsPipelineState* getPipelineState(Texture* renderTarget) const;

private:
	RenderDevice*                            device = nullptr;

	int32                                    rtvIndexForSwapChain = -1;
	std::vector<EPixelFormat>                rtvFormats;
	BufferedUniquePtr<GraphicsPipelineState> pipelineStates;

	VertexInputLayout                        inputLayout;
	VolatileDescriptorHelper                 passDescriptor;
};
