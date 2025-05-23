#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"

class RenderCommandList;
class ShaderResourceView;

struct ToneMappingInput
{
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
	void initialize();

	void renderToneMapping(RenderCommandList* commandList, uint32 swapchainIndex, const ToneMappingInput& passInput);

private:
	UniquePtr<GraphicsPipelineState> pipelineState;
	VertexInputLayout inputLayout;

	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
