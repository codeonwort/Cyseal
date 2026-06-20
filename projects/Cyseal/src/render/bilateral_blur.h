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
	Texture*             inMomentTexture; // x, y: first, second raw moment
	ShaderResourceView*  inMomentSRV;
	ShaderResourceView*  inSceneDepthSRV;
	ShaderResourceView*  inGBuffer0SRV;
	ShaderResourceView*  inGBuffer1SRV;
	Texture*             outColorTexture; // Could be same as inColorUAV
	UnorderedAccessView* outColorUAV;
	uint32               feedbackPhase; // If (n > 0), the result is copied back to inColorTexture after blur is applied n-th. Ignored if in/out textures are same.
};

// Performs variance-guided edge-avoiding a-trous wavelet transform.
class BilateralBlur : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inDevice);

	void renderBilateralBlur(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput);

private:
	void resizeTexture(RenderCommandList* commandList, uint32 width, uint32 height);

	void initPhase(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput);
	void blurPhase(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput);

	RenderDevice* device = nullptr;

	UniquePtr<ComputePipelineState> initPipelineState;
	VolatileDescriptorHelper initPassDescriptor;

	UniquePtr<ComputePipelineState> pipelineState;
	VolatileDescriptorHelper passDescriptor;

	UniquePtr<Texture> colorScratch;
	UniquePtr<UnorderedAccessView> colorScratchUAV;

	UniquePtr<Texture> varianceTextures[2];
	UniquePtr<UnorderedAccessView> varianceUAVs[2];
};
