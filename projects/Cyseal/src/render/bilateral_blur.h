#pragma once

#include "scene_render_pass.h"
#include "rhi/rhi_forward.h"
#include "util/volatile_descriptor.h"

struct BilateralBlurInput
{
	uint32               imageWidth;
	uint32               imageHeight;
	int32                blurCount;
	float                cPhi; // color weight
	float                nPhi; // normal weight
	float                pPhi; // position weight
	ConstantBufferView*  sceneUniformCBV;
	Texture*             inColorTexture;
	UnorderedAccessView* inColorUAV;
	ShaderResourceView*  inSceneDepthSRV;
	ShaderResourceView*  inGBuffer0SRV;
	ShaderResourceView*  inGBuffer1SRV;
	Texture*             outColorTexture; // Could be same as inColorUAV
	UnorderedAccessView* outColorUAV;
	Texture*             outColorHistory = nullptr; // If not null, the result of first iteration is copied to this texture.
};

class BilateralBlur : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inDevice);

	void renderBilateralBlur(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput);

private:
	void resizeTexture(RenderCommandList* commandList, uint32 width, uint32 height);

	RenderDevice* device = nullptr;

	UniquePtr<ComputePipelineState> pipelineState;
	VolatileDescriptorHelper passDescriptor;

	UniquePtr<Texture> colorScratch;
	UniquePtr<UnorderedAccessView> colorScratchUAV;
};
