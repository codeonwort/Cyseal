#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/rhi_forward.h"
#include "util/volatile_descriptor.h"

struct HiZPassInput
{
	uint32                                  textureWidth;
	uint32                                  textureHeight;
	ShaderResourceView*                     sceneDepthSRV;
	Texture*                                hizTexture;
	ShaderResourceView*                     hizSRV;
	BufferedUniquePtr<UnorderedAccessView>& hizUAVs;
};

// Generate HiZ texture from depth texture.
class HiZPass final : public SceneRenderPass
{
public:
	void initialize();

	void renderHiZ(RenderCommandList* commandList, uint32 swapchainIndex, const HiZPassInput& passInput);

private:
	UniquePtr<ComputePipelineState> copyPipeline;
	VolatileDescriptorHelper        copyPassDescriptor;

	UniquePtr<ComputePipelineState> downsamplePipeline;
	VolatileDescriptorHelper        downsamplePassDescriptor;
};
