#include "optical_flow_pass.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"

// cbuffer cbOF
struct PassUniform
{
	int32  iInputLumaResolution[2];
	uint32 uOpticalFlowPyramidLevel;
	uint32 uOpticalFlowPyramidLevelCount;

	uint32 iFrameIndex;
	uint32 backbufferTransferFunction;
	float  minMaxLuminance[2];
};

void OpticalFlowPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;

	initializePipelines();
}

void OpticalFlowPass::runOpticalFlow(RenderCommandList* commandList, uint32 swapchainIndex, const OpticalFlowPassInput& passInput)
{
	recreateResources(commandList, swapchainIndex, passInput);

	PassUniform uniformData{
		.iInputLumaResolution          = { passInput.lumaResolutionX, passInput.lumaResolutionY },
		.uOpticalFlowPyramidLevel      = 0,
		.uOpticalFlowPyramidLevelCount = 7,
		.iFrameIndex                   = passInput.frameIndex,
		.backbufferTransferFunction    = (uint32)passInput.transferFunction,
		.minMaxLuminance               = { 0.0f, 3000.0f }, // #wip: minMaxLuminance
	};

	ConstantBufferView* passUniformCBV = prepareLumaDescriptor.getUniformCBV(swapchainIndex);
	passUniformCBV->writeToGPU(commandList, &uniformData, sizeof(uniformData));

	{
		SCOPED_DRAW_EVENT(commandList, PrepareLuma);

		TextureBarrierAuto textureBarriersBefore[] = {
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				passInput.sceneColorTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				lumaTexture.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(textureBarriersBefore), textureBarriersBefore, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbOF", passUniformCBV);
		SPT.texture("r_input_color", passInput.sceneColorSRV);
		SPT.rwTexture("rw_optical_flow_input", lumaUAVs[0].get());

		prepareLumaDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());
		auto descriptorHeap = prepareLumaDescriptor.getDescriptorHeap(swapchainIndex);

		commandList->setComputePipelineState(pipelinePrepareLuma.get());
		commandList->bindComputeShaderParameters(pipelinePrepareLuma.get(), &SPT, descriptorHeap);

		uint32 dispatchSizeX = (passInput.lumaResolutionX + 15) / 16, dispatchSizeY = (passInput.lumaResolutionY + 15) / 16;
		commandList->dispatchCompute(dispatchSizeX, dispatchSizeY, 1);

		GlobalBarrier globalBarrierAfter{
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS
		};
		commandList->barrierAuto(0, nullptr, 0, nullptr, 1, &globalBarrierAfter);
	}
	{
		SCOPED_DRAW_EVENT(commandList, GenerateInputPyramid);

		// #wip: Execute GenerateInputPyramid
	}
}

void OpticalFlowPass::initializePipelines()
{
	const uint32 swapchainCount = device->maxFramesInFlight();

	lumaResolutionXs.resize(swapchainCount, 0);
	lumaResolutionYs.resize(swapchainCount, 0);

	prepareLumaDescriptor.initialize(L"OpticalFlowPrepareLuma", swapchainCount, sizeof(PassUniform));

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

void OpticalFlowPass::recreateResources(RenderCommandList* commandList, uint32 swapchainIndex, const OpticalFlowPassInput& passInput)
{
	if (lumaResolutionXs[swapchainIndex] != passInput.lumaResolutionX || lumaResolutionYs[swapchainIndex] != passInput.lumaResolutionY)
	{
		commandList->enqueueDeferredDealloc(lumaTexture.release(), true);
		for (size_t i = 0; i < _countof(lumaUAVs); ++i)
		{
			commandList->enqueueDeferredDealloc(lumaUAVs[i].release(), true);
		}

		lumaResolutionXs[swapchainIndex] = passInput.lumaResolutionX;
		lumaResolutionYs[swapchainIndex] = passInput.lumaResolutionY;

		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			EPixelFormat::R32_UINT, ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			passInput.lumaResolutionX, passInput.lumaResolutionY, 7);
		lumaTexture = UniquePtr<Texture>(device->createTexture(texDesc));
		lumaTexture->setDebugName(L"RT_OpticalFlow_Luma");

		for (uint32 mip = 0; mip < _countof(lumaUAVs); ++mip)
		{
			lumaUAVs[mip] = UniquePtr<UnorderedAccessView>(device->createUAV(lumaTexture.get(),
				UnorderedAccessViewDesc{
					.format         = lumaTexture->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = mip, .planeSlice = 0 },
				}
			));
		}
	}
}
