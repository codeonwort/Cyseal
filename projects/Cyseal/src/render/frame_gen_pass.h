#pragma once

#include "scene_render_pass.h"
#include "optical_flow_common.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "util/volatile_descriptor.h"

class Camera;

struct FrameGenPassInput
{
	const Camera*                         camera;
	int32                                 renderSizeX;
	int32                                 renderSizeY;
	int32                                 displaySizeX;
	int32                                 displaySizeY;
	float                                 deltaTime;
	uint32                                dispatchFlags;
	OpticalFlowBackbufferTransferFunction backBufferTransferFunction;
	bool                                  bReset;
	Texture*                              sceneDepthTexture;
	ShaderResourceView*                   sceneDepthSRV;
	Texture*                              motionVectorTexture;
	ShaderResourceView*                   motionVectorSRV;
};

class FrameGenPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);

private:
	void initializePipelines();

	void recreateResources(RenderCommandList* commandList, const FrameGenPassInput& passInput);

	void updateUniforms(RenderCommandList* commandList, const FrameGenPassInput& passInput);
	void preparePhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);
	void dispatchPhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);

	ConstantBufferView* getCurrentFrameInterpUniformCBV();
	ConstantBufferView* getCurrentInpaintingPyramidUniformCBV();

private:
	RenderDevice* device = nullptr;
	uint32 cpuFrameIndex = 0;

	// #todo-fsr3: See FfxFrameInterpolationPass enum in
	// <FidelityFX_SDK>\sdk\src\components\frameinterpolation\ffx_frameinterpolation.cpp
	UniquePtr<ComputePipelineState>        reconstructAndDilatePipeline;
	UniquePtr<ComputePipelineState>        setupPipeline;
	UniquePtr<ComputePipelineState>        reconstructPrevDepthPipeline;
	UniquePtr<ComputePipelineState>        gameMotionVectorFieldPipeline;
	UniquePtr<ComputePipelineState>        opticalFlowVectorFieldPipeline;
	UniquePtr<ComputePipelineState>        disocclusionMaskPipeline;
	UniquePtr<ComputePipelineState>        interpolationPipeline;
	UniquePtr<ComputePipelineState>        inpaintingPyramidPipeline;
	UniquePtr<ComputePipelineState>        inpaintingPipeline;
	UniquePtr<ComputePipelineState>        gameVectorFieldInpaintingPyramidPipeline;
	UniquePtr<ComputePipelineState>        debugViewPipeline;

	VolatileDescriptorHelper               frameInterpDescriptor;
	VolatileDescriptorHelper               inpaintingPyramidDescriptor;

	BufferedUniquePtr<Texture>             reconstructedPrevDepthTextures;
	BufferedUniquePtr<ShaderResourceView>  reconstructedPrevDepthSRVs;
	BufferedUniquePtr<UnorderedAccessView> reconstructedPrevDepthUAVs;
	BufferedUniquePtr<Texture>             dilatedMotionVectorTextures;
	BufferedUniquePtr<ShaderResourceView>  dilatedMotionVectorSRVs;
	BufferedUniquePtr<UnorderedAccessView> dilatedMotionVectorUAVs;
	BufferedUniquePtr<Texture>             dilatedDepthTextures;
	BufferedUniquePtr<ShaderResourceView>  dilatedDepthSRVs;
	BufferedUniquePtr<UnorderedAccessView> dilatedDepthUAVs;
};
