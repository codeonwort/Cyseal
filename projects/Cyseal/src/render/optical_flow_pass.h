#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "util/volatile_descriptor.h"

// See ffx_opticalflow_prepare_luma.h
enum class OpticalFlowBackbufferTransferFunction : uint32
{
	LinearLdrToLuminance                  = 0,
	PQCorrectedHdrToPerceivedLuminance    = 1,
	SCRGBCorrectedHdrToPerceivedLuminance = 2,

	Count,
};

struct OpticalFlowPassInput
{
	uint32                                frameIndex;
	OpticalFlowBackbufferTransferFunction transferFunction;
	int32                                 lumaResolutionX;
	int32                                 lumaResolutionY;
	Texture*                              sceneColorTexture;
	ShaderResourceView*                   sceneColorSRV;
};

class OpticalFlowPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void runOpticalFlow(RenderCommandList* commandList, uint32 swapchainIndex, const OpticalFlowPassInput& passInput);

private:
	void initializePipelines();
	void recreateResources(RenderCommandList* commandList, uint32 swapchainIndex, const OpticalFlowPassInput& passInput);

private:
	RenderDevice* device = nullptr;

	// <FidelityFX_SDK>/sdk/src/components/frameinterpolation/ffx_opticalflow_private.h
	UniquePtr<ComputePipelineState> pipelinePrepareLuma;
	UniquePtr<ComputePipelineState> pipelineGenerateOpticalFlowInputPyramid;
	UniquePtr<ComputePipelineState> pipelineGenerateSCDHistogram;
	UniquePtr<ComputePipelineState> pipelineComputeSCDDivergence;
	UniquePtr<ComputePipelineState> pipelineComputeOpticalFlowAdvancedV5;
	UniquePtr<ComputePipelineState> pipelineFilterOpticalFlowV5;
	UniquePtr<ComputePipelineState> pipelineScaleOpticalFlowAdvancedV5;

	VolatileDescriptorHelper        prepareLumaDescriptor;

	std::vector<int32>              lumaResolutionXs;
	std::vector<int32>              lumaResolutionYs;
	UniquePtr<Texture>              lumaTexture;
	UniquePtr<UnorderedAccessView>  lumaUAVs[7];
};
