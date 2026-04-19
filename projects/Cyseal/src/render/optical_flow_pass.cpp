#include "optical_flow_pass.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"

constexpr uint32 OpticalFlowMaxPyramidLevels = 7;
constexpr uint32 HistogramBins = 256;
constexpr uint32 HistogramsPerDim = 3;
constexpr uint32 HistogramShifts = 3;

static uint32 GetSCDHistogramTextureWidth()
{
	return HistogramBins * (HistogramsPerDim * HistogramsPerDim);
}

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

// cbuffer cbOF_SPD
struct SpdUniform
{
	uint32 mips;
	uint32 numWorkGroups;
	uint32 workGroupOffset[2];
	uint32 numWorkGroupOpticalFlowInputPyramid;
	uint32 pad0_;
	uint32 pad1_;
	uint32 pad2_;
};

// Ported from <FidelityFX_SDK>/sdk/include/FidelityFX/gpu/spd/ffx_spd.h
static void ffxSpdSetup(
	uint32 dispatchThreadGroupCountXY[2],
	uint32 workGroupOffset[2],
	uint32 numWorkGroupsAndMips[2],
	const uint32 rectInfo[4],
	const int32 mips)
{
	// determines the offset of the first tile to downsample based on
	// left (rectInfo[0]) and top (rectInfo[1]) of the subregion.
	workGroupOffset[0] = rectInfo[0] / 64;
	workGroupOffset[1] = rectInfo[1] / 64;

	uint32 endIndexX = (rectInfo[0] + rectInfo[2] - 1) / 64;  // rectInfo[0] = left, rectInfo[2] = width
	uint32 endIndexY = (rectInfo[1] + rectInfo[3] - 1) / 64;  // rectInfo[1] = top, rectInfo[3] = height

	// we only need to dispatch as many thread groups as tiles we need to downsample
	// number of tiles per slice depends on the subregion to downsample
	dispatchThreadGroupCountXY[0] = endIndexX + 1 - workGroupOffset[0];
	dispatchThreadGroupCountXY[1] = endIndexY + 1 - workGroupOffset[1];

	// number of thread groups per slice
	numWorkGroupsAndMips[0] = (dispatchThreadGroupCountXY[0]) * (dispatchThreadGroupCountXY[1]);

	if (mips >= 0)
	{
		numWorkGroupsAndMips[1] = uint32(mips);
	}
	else
	{
		// calculate based on rect width and height
		uint32 resolution = (std::max)(rectInfo[2], rectInfo[3]);
		numWorkGroupsAndMips[1] = (uint32)((std::min(std::floor(std::log2(float(resolution))), float(12))));
	}
}

void OpticalFlowPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;

	initializePipelines();
}

// See dispatch() in sdk/src/components/opticalflow/ffx_opticalflow.cpp for dispatch order.
void OpticalFlowPass::runOpticalFlow(RenderCommandList* commandList, uint32 swapchainIndex, const OpticalFlowPassInput& passInput)
{
	recreateResources(commandList, swapchainIndex, passInput);

	const uint32 currFrame = swapchainIndex;
	const uint32 prevFrame = (swapchainIndex + 1) % 2;

	PassUniform passUniformData{
		.iInputLumaResolution          = { passInput.lumaResolutionX, passInput.lumaResolutionY },
		.uOpticalFlowPyramidLevel      = 0,
		.uOpticalFlowPyramidLevelCount = 7,
		.iFrameIndex                   = passInput.frameIndex,
		.backbufferTransferFunction    = (uint32)passInput.transferFunction,
		.minMaxLuminance               = { 0.0f, 3000.0f }, // #wip: minMaxLuminance
	};
	ConstantBufferView* passUniformCBV = prepareLumaDescriptor.getUniformCBV(swapchainIndex);
	passUniformCBV->writeToGPU(commandList, &passUniformData, sizeof(passUniformData));

	uint32 threadGroupSizeOpticalFlowInputPyramid[2];
	uint32 workGroupOffset[2];
	uint32 numWorkGroupsAndMips[2];
	const uint32 rectInfo[4] = { 0u, 0u, (uint32)passInput.lumaResolutionX, (uint32)passInput.lumaResolutionY };
	// #wip: Why mips = 4, not 7? (result is correct based on PIX)
	ffxSpdSetup(threadGroupSizeOpticalFlowInputPyramid, workGroupOffset, numWorkGroupsAndMips, rectInfo, 4);

	SpdUniform spdUniformData{
		.mips                                = numWorkGroupsAndMips[1],
		.numWorkGroups                       = numWorkGroupsAndMips[0],
		.workGroupOffset                     = { workGroupOffset[0], workGroupOffset[1] },
		.numWorkGroupOpticalFlowInputPyramid = numWorkGroupsAndMips[0],
		.pad0_                               = 0,
		.pad1_                               = 0,
		.pad2_                               = 0,
	};
	ConstantBufferView* spdUniformCBV = genInputPyramidDescriptor.getUniformCBV(swapchainIndex);
	spdUniformCBV->writeToGPU(commandList, &spdUniformData, sizeof(spdUniformData));

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

		const int32 threadGroupSizeX = 16;
		const int32 threadGroupSizeY = 16;
		const uint32 threadPixelsX = 2;
		const uint32 threadPixelsY = 2;
		int32 dispatchX = ((passInput.lumaResolutionX + (threadPixelsX - 1)) / threadPixelsX + (threadGroupSizeX - 1)) / threadGroupSizeX;
		int32 dispatchY = ((passInput.lumaResolutionY + (threadPixelsY - 1)) / threadPixelsY + (threadGroupSizeY - 1)) / threadGroupSizeY;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		GlobalBarrier globalBarrierAfter{
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS
		};
		commandList->barrierAuto(0, nullptr, 0, nullptr, 1, &globalBarrierAfter);
	}
	{
		SCOPED_DRAW_EVENT(commandList, GenerateInputPyramid);

		// The only texture parameter was barriered right before, so no need to place any barriers here.

		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbOF", passUniformCBV);
		SPT.constantBuffer("cbOF_SPD", spdUniformCBV);
		SPT.rwTexture("rw_optical_flow_input", lumaUAVs[0].get());
		SPT.rwTexture("rw_optical_flow_input_level_1", lumaUAVs[1].get());
		SPT.rwTexture("rw_optical_flow_input_level_2", lumaUAVs[2].get());
		SPT.rwTexture("rw_optical_flow_input_level_3", lumaUAVs[3].get());
		SPT.rwTexture("rw_optical_flow_input_level_4", lumaUAVs[4].get());
		SPT.rwTexture("rw_optical_flow_input_level_5", lumaUAVs[5].get());
		SPT.rwTexture("rw_optical_flow_input_level_6", lumaUAVs[6].get());

		genInputPyramidDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());
		auto descriptorHeap = genInputPyramidDescriptor.getDescriptorHeap(swapchainIndex);

		commandList->setComputePipelineState(pipelineGenerateOpticalFlowInputPyramid.get());
		commandList->bindComputeShaderParameters(pipelineGenerateOpticalFlowInputPyramid.get(), &SPT, descriptorHeap);

		const uint32 dispatchX = threadGroupSizeOpticalFlowInputPyramid[0];
		const uint32 dispatchY = threadGroupSizeOpticalFlowInputPyramid[1];
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		GlobalBarrier globalBarrierAfter{
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS
		};
		commandList->barrierAuto(0, nullptr, 0, nullptr, 1, &globalBarrierAfter);
	}
	{
		SCOPED_DRAW_EVENT(commandList, GenerateSCDHistogram);

		TextureBarrierAuto textureBarriersBefore[] = {
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				lumaTexture.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				scdHistogramTextures[currFrame].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(textureBarriersBefore), textureBarriersBefore, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbOF", passUniformCBV);
		SPT.texture("r_optical_flow_input", lumaSRV.get());
		SPT.rwTexture("rw_optical_flow_scd_histogram", scdHistogramUAVs[currFrame].get());

		genSCDHistogramDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());
		auto descriptorHeap = genSCDHistogramDescriptor.getDescriptorHeap(swapchainIndex);

		commandList->setComputePipelineState(pipelineGenerateSCDHistogram.get());
		commandList->bindComputeShaderParameters(pipelineGenerateSCDHistogram.get(), &SPT, descriptorHeap);

		const uint32 threadGroupSizeX = 32;
		const uint32 threadGroupSizeY = 8;
		const uint32 strataWidth = (passInput.lumaResolutionX / 4) / HistogramsPerDim;
		const uint32 strataHeight = passInput.lumaResolutionY / HistogramsPerDim;
		const uint32 dispatchX = (strataWidth + threadGroupSizeX - 1) / threadGroupSizeX;
		const uint32 dispatchY = 16;
		const uint32 dispatchZ = HistogramsPerDim * HistogramsPerDim;
		commandList->dispatchCompute(dispatchX, dispatchY, dispatchZ);

		GlobalBarrier globalBarrierAfter{
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS
		};
		commandList->barrierAuto(0, nullptr, 0, nullptr, 1, &globalBarrierAfter);
	}
	{
		SCOPED_DRAW_EVENT(commandList, ComputeSCDDivergence);

		TextureBarrierAuto textureBarriersBefore[] = {
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				scdHistogramTextures[currFrame].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				scdHistogramTextures[prevFrame].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				scdTempTexture.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				scdOutputTexture.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(textureBarriersBefore), textureBarriersBefore, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbOF", passUniformCBV);
		SPT.rwTexture("rw_optical_flow_scd_histogram", scdHistogramUAVs[currFrame].get());
		SPT.rwTexture("rw_optical_flow_scd_previous_histogram", scdHistogramUAVs[prevFrame].get());
		SPT.rwTexture("rw_optical_flow_scd_temp", scdTempUAV.get());
		SPT.rwTexture("rw_optical_flow_scd_output", scdOutputUAV.get());

		computeSCDDivergenceDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());
		auto descriptorHeap = computeSCDDivergenceDescriptor.getDescriptorHeap(swapchainIndex);

		commandList->setComputePipelineState(pipelineComputeSCDDivergence.get());
		commandList->bindComputeShaderParameters(pipelineComputeSCDDivergence.get(), &SPT, descriptorHeap);

		const uint32 dispatchX = HistogramsPerDim * HistogramsPerDim;
		const uint32 dispatchY = HistogramShifts;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		GlobalBarrier globalBarrierAfter{
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS
		};
		commandList->barrierAuto(0, nullptr, 0, nullptr, 1, &globalBarrierAfter);
	}
	// #wip: Dispatch pipelineComputeOpticalFlowAdvancedV5
	// #wip: Dispatch pipelineFilterOpticalFlowV5
	// #wip: Dispatch pipelineScaleOpticalFlowAdvancedV5
}

void OpticalFlowPass::initializePipelines()
{
	const uint32 swapchainCount = device->maxFramesInFlight();

	lumaResolutionXs.resize(swapchainCount, 0);
	lumaResolutionYs.resize(swapchainCount, 0);

	scdHistogramTextures.initialize(swapchainCount);
	scdHistogramUAVs.initialize(swapchainCount);

	prepareLumaDescriptor.initialize(L"OpticalFlowPrepareLuma", swapchainCount, sizeof(PassUniform));
	genInputPyramidDescriptor.initialize(L"OpticalFlowGenerateInputPyramid", swapchainCount, sizeof(SpdUniform));
	genSCDHistogramDescriptor.initialize(L"OpticalFlowGenerateSCDHistogram", swapchainCount, 0);
	computeSCDDivergenceDescriptor.initialize(L"OpticalFlowComputeSCDDivergence", swapchainCount, 0);

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
		lumaSRV = UniquePtr<ShaderResourceView>(device->createSRV(lumaTexture.get(),
			ShaderResourceViewDesc{
				.format              = lumaTexture->getCreateParams().format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = lumaTexture->getCreateParams().mipLevels,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
	}
	if (scdHistogramTextures[0] == nullptr)
	{
		for (uint32 i = 0; i < scdHistogramTextures.size(); ++i)
		{
			scdHistogramTextures[i] = UniquePtr<Texture>(device->createTexture(TextureCreateParams::texture2D(
				EPixelFormat::R32_UINT, ETextureAccessFlags::UAV,
				GetSCDHistogramTextureWidth(), 1)));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"OpticalFlow_SCDHistogram_%u", i);
			scdHistogramTextures[i]->setDebugName(debugName);

			scdHistogramUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(scdHistogramTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = lumaTexture->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}
	if (scdTempTexture == nullptr)
	{
		scdTempTexture = UniquePtr<Texture>(device->createTexture(TextureCreateParams::texture2D(
			EPixelFormat::R32_UINT, ETextureAccessFlags::UAV, 3, 1)));
		scdTempTexture->setDebugName(L"OpticalFlow_SCDTemp");

		scdTempUAV = UniquePtr<UnorderedAccessView>(device->createUAV(scdTempTexture.get(),
			UnorderedAccessViewDesc{
				.format         = scdTempTexture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			}
		));
	}
	if (scdOutputTexture == nullptr)
	{
		scdOutputTexture = UniquePtr<Texture>(device->createTexture(TextureCreateParams::texture2D(
			EPixelFormat::R32_UINT, ETextureAccessFlags::UAV, 3, 1)));
		scdOutputTexture->setDebugName(L"OpticalFlow_SCDOutput");

		scdOutputUAV = UniquePtr<UnorderedAccessView>(device->createUAV(scdOutputTexture.get(),
			UnorderedAccessViewDesc{
				.format         = scdOutputTexture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			}
		));
	}
}
