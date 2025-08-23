#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/rhi_forward.h"

struct HiZPassInput
{
	uint32                                  textureWidth;
	uint32                                  textureHeight;
	ShaderResourceView*                     sceneDepthSRV;
	BufferedUniquePtr<UnorderedAccessView>& hizUAVs;
};

// Generate HiZ texture from depth texture.
class HiZPass final : public SceneRenderPass
{
public:
	void initialize();

	void renderHiZ(RenderCommandList* commandList, uint32 swapchainIndex, const HiZPassInput& passInput);

private:
	UniquePtr<GraphicsPipelineState> pipelineState;
};
