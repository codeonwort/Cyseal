#pragma once

#include "scene_render_pass.h"
#include "rhi/rhi_forward.h"
#include "util/volatile_descriptor.h"
#include "util/texture_sequence.h"

struct StoreHistoryPassInput
{
	uint32               textureWidth;
	uint32               textureHeight;
	Texture*             gbuffer0;
	Texture*             gbuffer1;
	ShaderResourceView*  gbuffer0SRV;
	ShaderResourceView*  gbuffer1SRV;
};

struct StoreHistoryPassResources
{
	Texture*             currNormal;
	ShaderResourceView*  currNormalSRV;
	UnorderedAccessView* currNormalUAV;

	Texture*             prevNormal;
	ShaderResourceView*  prevNormalSRV;
	UnorderedAccessView* prevNormalUAV;

	Texture*             currRoughness;
	ShaderResourceView*  currRoughnessSRV;
	UnorderedAccessView* currRoughnessUAV;

	Texture*             prevRoughness;
	ShaderResourceView*  prevRoughnessSRV;
	UnorderedAccessView* prevRoughnessUAV;
};

class StoreHistoryPass : public SceneRenderPass
{
public:
	void initialize(RenderDevice* renderDevice);

	void extractCurrent(RenderCommandList* commandList, uint32 swapchainIndex, const StoreHistoryPassInput& passInput);

	void copyCurrentToPrev(RenderCommandList* commandList, uint32 swapchainIndex);

	StoreHistoryPassResources getResources(uint32 swapchainIndex) const;

private:
	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);

private:
	UniquePtr<ComputePipelineState> copyPipeline;
	VolatileDescriptorHelper        passDescriptor;

	uint32                          historyWidth = 0;
	uint32                          historyHeight = 0;
	TextureSequence                 normalHistory;
	TextureSequence                 roughnessHistory;
};
