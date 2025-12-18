#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "util/volatile_descriptor.h"

struct FrameGenPassInput
{
	//
};

class FrameGenPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);

private:
	void initializePipelines();

private:
	RenderDevice* device = nullptr;

	// #todo-fsr3: See FfxFrameInterpolationPass enum in
	// <FidelityFX_SDK>\sdk\src\components\frameinterpolation\ffx_frameinterpolation.cpp
	UniquePtr<ComputePipelineState> reconstructAndDilatePipeline;
	UniquePtr<ComputePipelineState> setupPipeline;
	UniquePtr<ComputePipelineState> reconstructPrevDepthPipeline;
	UniquePtr<ComputePipelineState> gameMotionVectorFieldPipeline;
	UniquePtr<ComputePipelineState> opticalFlowVectorFieldPipeline;
	UniquePtr<ComputePipelineState> disocclusionMaskPipeline;
	UniquePtr<ComputePipelineState> interpolationPipeline;
	UniquePtr<ComputePipelineState> inpaintingPyramidPipeline;
	UniquePtr<ComputePipelineState> inpaintingPipeline;
	UniquePtr<ComputePipelineState> gameVectorFieldInpaintingPyramidPipeline;
	UniquePtr<ComputePipelineState> debugViewPipeline;
};
