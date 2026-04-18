#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "util/volatile_descriptor.h"

class Camera;

struct FrameGenPassInput
{
	const Camera* camera;

	int32  renderSizeX;
	int32  renderSizeY;
	int32  displaySizeX;
	int32  displaySizeY;
	float  deltaTime;
	int32  opticalFlowBlockSize;
	uint32 dispatchFlags;
	uint32 backBufferTransferFunction;
};

class FrameGenPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);

private:
	void initializePipelines();

	void preparePhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);
	void dispatchPhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);

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

	VolatileDescriptorHelper        frameInterpDescriptor;
	VolatileDescriptorHelper        inpaintingPyramidDescriptor;
};
