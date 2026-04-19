#include "optical_flow_pass.h"
#include "rhi/render_device.h"

void OpticalFlowPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;

	initializePipelines();
}

void OpticalFlowPass::runOpticalFlow(RenderCommandList* commandList, uint32 swapchainIndex, const OpticalFlowPassInput& passInput)
{

}

void OpticalFlowPass::initializePipelines()
{
	const uint32 swapchainCount = device->maxFramesInFlight();

	auto createPipeline = [device = this->device]
		(const char* debugName, const wchar_t* filepath, UniquePtr<ComputePipelineState>& pipeline)
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, debugName);
		shader->declarePushConstants();
		shader->loadFromFile(filepath, "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		pipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0 }
		));

		delete shader;
	};

	createPipeline("OpticalFlowPrepareLumaCS", L"amd/ffx_opticalflow_prepare_luma_pass.hlsl", pipelinePrepareLuma);
	createPipeline("OpticalFlowGenerateInputPyramidCS", L"amd/ffx_opticalflow_compute_luminance_pyramid_pass.hlsl", pipelineGenerateOpticalFlowInputPyramid);
	createPipeline("OpticalFlowGenerateSCDHistogramCS", L"amd/ffx_opticalflow_generate_scd_histogram_pass.hlsl", pipelineGenerateSCDHistogram);
	createPipeline("OpticalFlowComputeSCDDivergence", L"amd/ffx_opticalflow_compute_scd_divergence_pass.hlsl", pipelineComputeSCDDivergence);
	createPipeline("OpticalFlowFilterAdvancedV5", L"amd/ffx_opticalflow_compute_optical_flow_advanced_pass_v5.hlsl", pipelineComputeOpticalFlowAdvancedV5);
	createPipeline("OpticalFlowFilterV5", L"amd/ffx_opticalflow_prepare_luma_pass.hlsl", pipelineFilterOpticalFlowV5);
	createPipeline("OpticalFlowScaleAdvancedV5", L"amd/ffx_opticalflow_scale_optical_flow_advanced_pass_v5.hlsl", pipelineScaleOpticalFlowAdvancedV5);
}
