#pragma once

#include "scene_render_pass.h"
#include "rhi/rhi_forward.h"
#include "util/volatile_descriptor.h"

struct StoreHistoryPassInput
{
	uint32               textureWidth;
	uint32               textureHeight;
	Texture*             gbuffer0;
	Texture*             gbuffer1;
	ShaderResourceView*  gbuffer0SRV;
	ShaderResourceView*  gbuffer1SRV;
	Texture*             prevNormalTexture;
	UnorderedAccessView* prevNormalUAV; // writeonly
	Texture*             prevRoughnessTexture;
	UnorderedAccessView* prevRoughnessUAV; // writeonly
};

class StoreHistoryPass : public SceneRenderPass
{
public:
	void initialize(RenderDevice* renderDevice);

	void renderHistory(RenderCommandList* commandList, uint32 swapchainIndex, const StoreHistoryPassInput& passInput);

private:
	UniquePtr<ComputePipelineState> copyPipeline;

	VolatileDescriptorHelper passDescriptor;
};
