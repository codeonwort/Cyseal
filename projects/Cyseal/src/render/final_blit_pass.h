#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include "util/volatile_descriptor.h"

class RenderCommandList;
class ShaderResourceView;

struct FinalBlitPassInput
{
	ConstantBufferView* sceneUniformCBV;
	Texture*            renderTarget; // If null we're rendering to backbuffer.
	ShaderResourceView* finalSceneColorSRV;
};

class FinalBlitPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void renderFinalBlit(RenderCommandList* commandList, uint32 swapchainIndex, const FinalBlitPassInput& passInput);

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
