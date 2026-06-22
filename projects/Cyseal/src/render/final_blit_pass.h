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
	RenderTargetView*   renderTargetRTV;
	Texture*            sourceTexture;
	ShaderResourceView* sourceSRV;
};

// #wip: Rename to just 'BlitPass'.
// - Blit a texture to another texture or swapchain buffer.
// - Useful when source and target textures have different sizes or pixel formats.
class FinalBlitPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice, uint32 inMaxBlitOperationsPerFrame);

	// Invoke every frame before calling renderFinalBlit().
	void resetBlitResources();

	/// - Be aware that blit source is transitioned to ShaderResource and blit target to RenderTarget.
	/// - Does not set viewport and scissor rect. You need to set them yourself.
	void renderFinalBlit(RenderCommandList* commandList, const FrameInfo& frameInfo, const FinalBlitPassInput& passInput);

private:
	GraphicsPipelineState* getPipelineState(Texture* renderTarget) const;

private:
	RenderDevice*                            device = nullptr;

	int32                                    rtvIndexForSwapChain = -1;
	std::vector<EPixelFormat>                rtvFormats;
	BufferedUniquePtr<GraphicsPipelineState> pipelineStates;

	VertexInputLayout                        inputLayout;
	VolatileDescriptorHelper                 passDescriptor;
	DescriptorIndexTracker                   descriptorIndexTracker;
	uint32                                   maxBlitOperationsPerFrame = 1;
	uint32                                   currentBlitOperations = 0xffffffff;
};
