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
	class ClearResourcePass*              clearResourcePass;
	OpticalFlowBackbufferTransferFunction transferFunction;
	bool                                  bResetAccumulation;
	uint32                                containerSizeX;
	uint32                                containerSizeY;
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

	Texture* getOpticalFlowVectorTexture() const;
	ShaderResourceView* getOpticalFlowVectorSRV() const;
	uint32 getOpticalFlowVectorSizeX() const;
	uint32 getOpticalFlowVectorSizeY() const;

private:
	void initializePipelines();
	void recreateResources(RenderCommandList* commandList, uint32 swapchainIndex, const OpticalFlowPassInput& passInput);

private:
	RenderDevice* device = nullptr;
	uint32 resourceFrameIndex = 0; // for CPU
	uint32 gpuFrameIndex = 0;
	bool bFirstExecution = true;

	// <FidelityFX_SDK>/sdk/src/components/opticalflow/ffx_opticalflow_private.h
	UniquePtr<ComputePipelineState>        pipelinePrepareLuma;
	UniquePtr<ComputePipelineState>        pipelineGenerateOpticalFlowInputPyramid;
	UniquePtr<ComputePipelineState>        pipelineGenerateSCDHistogram;
	UniquePtr<ComputePipelineState>        pipelineComputeSCDDivergence;
	UniquePtr<ComputePipelineState>        pipelineComputeOpticalFlowAdvancedV5;
	UniquePtr<ComputePipelineState>        pipelineFilterOpticalFlowV5;
	UniquePtr<ComputePipelineState>        pipelineScaleOpticalFlowAdvancedV5;

	VolatileDescriptorHelper               prepareLumaDescriptor;
	VolatileDescriptorHelper               genInputPyramidDescriptor;
	VolatileDescriptorHelper               genSCDHistogramDescriptor;
	VolatileDescriptorHelper               computeSCDDivergenceDescriptor;
	VolatileDescriptorHelper               computeOpticalFlowAdvancedV5Descriptor;
	VolatileDescriptorHelper               filterOpticalFlowV5Descriptor;
	VolatileDescriptorHelper               scaleOpticalFlowAdvancedV5Descriptor;

	std::vector<int32>                     containerResolutionXs;
	std::vector<int32>                     containerResolutionYs;
	std::vector<int32>                     lumaResolutionXs;
	std::vector<int32>                     lumaResolutionYs;

	UniquePtr<Texture>                     opticalFlowInputTextures[2][7];
	UniquePtr<UnorderedAccessView>         opticalFlowInputUAVs[2][7];
	UniquePtr<ShaderResourceView>          opticalFlowInputSRVs[2][7];

	UniquePtr<Texture>                     opticalFlowTextures[2][7];
	UniquePtr<UnorderedAccessView>         opticalFlowUAVs[2][7];
	UniquePtr<ShaderResourceView>          opticalFlowSRVs[2][7];

	BufferedUniquePtr<Texture>             scdHistogramTextures;
	BufferedUniquePtr<UnorderedAccessView> scdHistogramUAVs;
	UniquePtr<Texture>                     scdTempTexture;
	UniquePtr<UnorderedAccessView>         scdTempUAV;
	UniquePtr<Texture>                     scdOutputTexture;
	UniquePtr<UnorderedAccessView>         scdOutputUAV;

	uint32                                 opticalFlowVectorSizeX = 0;
	uint32                                 opticalFlowVectorSizeY = 0;
	UniquePtr<Texture>                     opticalFlowVectorTexture;
	UniquePtr<UnorderedAccessView>         opticalFlowVectorUAV;
	UniquePtr<ShaderResourceView>          opticalFlowVectorSRV;
};
