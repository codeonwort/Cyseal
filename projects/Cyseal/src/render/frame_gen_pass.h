#pragma once

#include "scene_render_pass.h"
#include "optical_flow_common.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "util/volatile_descriptor.h"

class Camera;

enum class EFrameGenDispatchFlags : uint32
{
	NONE                        = 0,
	DRAW_DEBUG_TEAR_LINES       = (1 << 0),
	DRAW_DEBUG_RESET_INDICATORS = (1 << 1),
	DRAW_DEBUG_VIEW             = (1 << 2),
};
ENUM_CLASS_FLAGS(EFrameGenDispatchFlags);

struct FrameGenPassInput
{
	class ClearResourcePass*              clearResourcePass;
	const OpticalFlowPassOutput*          opticalFlowPassOutput;
	const Camera*                         camera;
	int32                                 renderSizeX;
	int32                                 renderSizeY;
	int32                                 displaySizeX;
	int32                                 displaySizeY;
	uint32                                frameID;
	EFrameGenDispatchFlags                dispatchFlags;
	OpticalFlowBackbufferTransferFunction backBufferTransferFunction;
	bool                                  bReset;
	float                                 minLuminance;
	float                                 maxLuminance;
	Texture*                              sceneColorTexture;
	ShaderResourceView*                   sceneColorSRV;
	Texture*                              sceneDepthTexture;
	ShaderResourceView*                   sceneDepthSRV;
	Texture*                              motionVectorTexture;
	ShaderResourceView*                   motionVectorSRV;
};

struct FrameGenPassOutput
{
	Texture*               interpolatedFrameTexture                = nullptr;
	ShaderResourceView*    interpolatedFrameSRV                    = nullptr;
	Texture*               opticalFlowMotionVectorFieldTextures[2] = { nullptr, nullptr };
	ShaderResourceView*    opticalFlowMotionVectorFieldSRVs[2]     = { nullptr, nullptr };
};

class FrameGenPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	FrameGenPassOutput runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);

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
	uint32 prevFrameID = 0; // from FrameGenPassInput
	uint32 interpolationDispatchCount = 0;
	bool bResetCurrentFrame = false;

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

	VolatileDescriptorHelper               prepareDescriptor;
	VolatileDescriptorHelper               frameInterpDescriptor;
	VolatileDescriptorHelper               inpaintingPyramidDescriptor;

	VolatileDescriptorHelper               reconstructPrevDepthDescriptor;
	VolatileDescriptorHelper               gameMotionVectorFieldDescriptor;
	VolatileDescriptorHelper               gameMotionVectorFieldInpaintingPyramidDescriptor;
	VolatileDescriptorHelper               opticalFlowVectorFieldDescriptor;
	VolatileDescriptorHelper               disocclusionMaskDescriptor;
	VolatileDescriptorHelper               interpolationDescriptor;
	VolatileDescriptorHelper               inpaintingDescriptor;

	UniquePtr<Texture>                     reconstructedPrevDepthTexture;
	UniquePtr<ShaderResourceView>          reconstructedPrevDepthSRV;
	UniquePtr<UnorderedAccessView>         reconstructedPrevDepthUAV;
	UniquePtr<Texture>                     reconstructedDepthInterpolatedFrameTexture;
	UniquePtr<ShaderResourceView>          reconstructedDepthInterpolatedFrameSRV;
	UniquePtr<UnorderedAccessView>         reconstructedDepthInterpolatedFrameUAV;
	UniquePtr<Texture>                     dilatedMotionVectorTexture;
	UniquePtr<ShaderResourceView>          dilatedMotionVectorSRV;
	UniquePtr<UnorderedAccessView>         dilatedMotionVectorUAV;
	UniquePtr<Texture>                     dilatedDepthTexture;
	UniquePtr<ShaderResourceView>          dilatedDepthSRV;
	UniquePtr<UnorderedAccessView>         dilatedDepthUAV;
	UniquePtr<Texture>                     gameMotionVectorFieldTextures[2]; // x, y
	UniquePtr<ShaderResourceView>          gameMotionVectorFieldSRVs[2]; // x, y
	UniquePtr<UnorderedAccessView>         gameMotionVectorFieldUAVs[2]; // x, y
	UniquePtr<Texture>                     opticalFlowMotionVectorFieldTextures[2]; // x, y
	UniquePtr<ShaderResourceView>          opticalFlowMotionVectorFieldSRVs[2]; // x, y
	UniquePtr<UnorderedAccessView>         opticalFlowMotionVectorFieldUAVs[2]; // x, y
	UniquePtr<Texture>                     disocclusionMaskTexture;
	UniquePtr<ShaderResourceView>          disocclusionMaskSRV;
	UniquePtr<UnorderedAccessView>         disocclusionMaskUAV;
	UniquePtr<Buffer>                      counterBuffer;
	UniquePtr<ShaderResourceView>          counterSRV;
	UniquePtr<UnorderedAccessView>         counterUAV;
	UniquePtr<Texture>                     defaultDistortionFieldTexture;
	UniquePtr<ShaderResourceView>          defaultDistortionFieldSRV;
	UniquePtr<Texture>                     prevInterpolationSourceTexture;
	UniquePtr<ShaderResourceView>          prevInterpolationSourceSRV;
	UniquePtr<UnorderedAccessView>         prevInterpolationSourceUAV;
	UniquePtr<Texture>                     inpaintingPyramidTexture;
	UniquePtr<ShaderResourceView>          inpaintingPyramidSRV;
	UniquePtr<UnorderedAccessView>         inpaintingPyramidUAVs[13];
	UniquePtr<Texture>                     opticalFlowConfidenceTexture; // #todo-fsr3: Probably dead code.
	UniquePtr<ShaderResourceView>          opticalFlowConfidenceSRV;
	UniquePtr<Texture>                     interpolationOutputTexture;
	UniquePtr<ShaderResourceView>          interpolationOutputSRV;
	UniquePtr<UnorderedAccessView>         interpolationOutputUAV;
};
