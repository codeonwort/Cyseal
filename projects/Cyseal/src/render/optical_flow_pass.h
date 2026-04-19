#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "util/volatile_descriptor.h"

struct OpticalFlowPassInput
{
	//
};

class OpticalFlowPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void runOpticalFlow(RenderCommandList* commandList, uint32 swapchainIndex, const OpticalFlowPassInput& passInput);

private:
	void initializePipelines();

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

	VolatileDescriptorHelper passDescriptor;
};
