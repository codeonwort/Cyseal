#include "frame_gen_pass.h"
#include "render/util/clear_resource_pass.h"
#include "rhi/rhi_policy.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "world/camera.h"

// doc: docs/techniques/frame-interpolation.md
// src: sdk/src/components/frameinterpolation/ffx_frameinterpolation.cpp

// Input: 2 back buffers + several resources shared with FSR3Upscaler and FfxOpticalFlow
// Output: An interpolated image between the 2 back buffers
// 

// FFX_DECLARE_CB(FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION)
struct FrameInterpUniform
{
	int32           renderSize[2];
	int32           displaySize[2];

	float           displaySizeRcp[2];
	float           cameraNear;
	float           cameraFar;

	int32           upscalerTargetSize[2];
	int32           Mode;
	int32           reset;

	float           fDeviceToViewDepth[4]; // #wip: setupDeviceDepthToViewSpaceDepthParams

	float           deltaTime; // In milliseconds
	int32           HUDLessAttachedFactor; // 1 or 0 (#wip: Is it for HUD upscaling?)
	int32           distortionFieldSize[2];

	float           opticalFlowScale[2];
	int32           opticalFlowBlockSize;
	uint32          dispatchFlags; // #wip: FfxFrameInterpolationDispatchFlags (FFX_FRAMEINTERPOLATION_DISPATCH_DRAW_DEBUG_TEAR_LINES, FFX_FRAMEINTERPOLATION_DISPATCH_DRAW_DEBUG_VIEW)

	int32           maxRenderSize[2];
	int32           opticalFlowHalfResMode;
	int32           NumInstances;

	int32           interpolationRectBase[2];
	int32           interpolationRectSize[2];

	float           debugBarColor[3];
	uint32          backBufferTransferFunction; // Transfer fn to convert interpolation source color data to linear RGB

	float           minMaxLuminance[2]; // Used when converting HDR colors to linear RGB
	float           fTanHalfFOV;
	int32           _pad1;

	float           fJitter[2];
	float           fMotionVectorScale[2];
};

// FFX_DECLARE_CB(FFX_FRAMEINTERPOLATION_BIND_CB_INPAINTING_PYRAMID)
struct InpaintingPyramidUniform
{
	uint32          mips;
	uint32          numWorkGroups;
	uint32          workGroupOffset[2];
};

void FrameGenPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;
	cpuFrameIndex = 0;

	const uint32 swapchainCount = 2;
	reconstructedPrevDepthTextures.initialize(swapchainCount);
	reconstructedPrevDepthSRVs.initialize(swapchainCount);
	reconstructedPrevDepthUAVs.initialize(swapchainCount);
	dilatedMotionVectorTextures.initialize(swapchainCount);
	dilatedMotionVectorSRVs.initialize(swapchainCount);
	dilatedMotionVectorUAVs.initialize(swapchainCount);
	dilatedDepthTextures.initialize(swapchainCount);
	dilatedDepthSRVs.initialize(swapchainCount);
	dilatedDepthUAVs.initialize(swapchainCount);

	initializePipelines();
}

void FrameGenPass::runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	recreateResources(commandList, passInput);

	updateUniforms(commandList, passInput);
	preparePhase(commandList, swapchainIndex, passInput);
	//dispatchPhase(commandList, swapchainIndex, passInput);

	cpuFrameIndex += 1;
}

void FrameGenPass::initializePipelines()
{
	const uint32 swapchainCount = 2;//device->maxFramesInFlight(); // Always need 2

	prepareDescriptor.initialize(device, L"FSR3_Prepare", swapchainCount, 0);
	frameInterpDescriptor.initialize(device, L"FSR3_FrameInterpUniform", swapchainCount, sizeof(FrameInterpUniform));
	inpaintingPyramidDescriptor.initialize(device, L"FSR3_InpaintingPyramidUniform", swapchainCount, sizeof(InpaintingPyramidUniform));

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

	createPipeline("FSR3ReconstructAndDilateCS", L"amd/ffx_frameinterpolation_reconstruct_and_dilate_pass.hlsl", reconstructAndDilatePipeline);
	createPipeline("FSR3SetupCS", L"amd/ffx_frameinterpolation_setup_pass.hlsl", setupPipeline);
	createPipeline("FSR3ReconstructPrevDepthCS", L"amd/ffx_frameinterpolation_reconstruct_previous_depth_pass.hlsl", reconstructPrevDepthPipeline);
	createPipeline("FSR3GameMotionVectorFieldPipelineCS", L"amd/ffx_frameinterpolation_game_motion_vector_field_pass.hlsl", gameMotionVectorFieldPipeline);
	createPipeline("FSR3OpticalFlowVectorFieldPipelineCS", L"amd/ffx_frameinterpolation_optical_flow_vector_field_pass.hlsl", opticalFlowVectorFieldPipeline);
	createPipeline("FSR3DisocclusionMaskPipelineCS", L"amd/ffx_frameinterpolation_disocclusion_mask_pass.hlsl", disocclusionMaskPipeline);
	createPipeline("FSR3InterpolationPipelineCS", L"amd/ffx_frameinterpolation_pass.hlsl", interpolationPipeline);
	createPipeline("FSR3InpaintingPyramidPipelineCS", L"amd/ffx_frameinterpolation_compute_inpainting_pyramid_pass.hlsl", inpaintingPyramidPipeline);
	createPipeline("FSR3InpaintingPipelineCS", L"amd/ffx_frameinterpolation_inpainting_pass.hlsl", inpaintingPipeline);
	createPipeline("FSR3GameVectorFieldInpaintingPyramidPipelineCS", L"amd/ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass.hlsl", gameVectorFieldInpaintingPyramidPipeline);
	createPipeline("FSR3DebugViewPipelineCS", L"amd/ffx_frameinterpolation_debug_view_pass.hlsl", debugViewPipeline);
}

void FrameGenPass::recreateResources(RenderCommandList* commandList, const FrameGenPassInput& passInput)
{
	for (size_t i = 0; i < reconstructedPrevDepthTextures.size(); ++i)
	{
		if (reconstructedPrevDepthTextures[i] == nullptr
			|| reconstructedPrevDepthTextures[i]->getCreateParams().width != passInput.displaySizeX
			|| reconstructedPrevDepthTextures[i]->getCreateParams().height != passInput.displaySizeY)
		{
			commandList->enqueueDeferredDealloc(reconstructedPrevDepthTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(reconstructedPrevDepthSRVs[i].release(), true);
			commandList->enqueueDeferredDealloc(reconstructedPrevDepthUAVs[i].release(), true);

			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R32_FLOAT,
				ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			reconstructedPrevDepthTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_ReconstructedPrevDepth_%u", (uint32)i);
			reconstructedPrevDepthTextures[i]->setDebugName(debugName);

			reconstructedPrevDepthSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(
				reconstructedPrevDepthTextures[i].get(),
				ShaderResourceViewDesc{
					.format              = reconstructedPrevDepthTextures[i]->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = 1,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					},
				}
			));
			reconstructedPrevDepthUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				reconstructedPrevDepthTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = reconstructedPrevDepthTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}

	for (size_t i = 0; i < dilatedMotionVectorTextures.size(); ++i)
	{
		if (dilatedMotionVectorTextures[i] == nullptr
			|| dilatedMotionVectorTextures[i]->getCreateParams().width != passInput.displaySizeX
			|| dilatedMotionVectorTextures[i]->getCreateParams().height != passInput.displaySizeY)
		{
			commandList->enqueueDeferredDealloc(dilatedMotionVectorTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(dilatedMotionVectorSRVs[i].release(), true);
			commandList->enqueueDeferredDealloc(dilatedMotionVectorUAVs[i].release(), true);

			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R16G16_FLOAT,
				// #wip: (FFX_RESOURCE_USAGE_RENDERTARGET | FFX_RESOURCE_USAGE_UAV | FFX_RESOURCE_USAGE_DCC_RENDERTARGET) ???
				ETextureAccessFlags::RTV | ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			dilatedMotionVectorTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_DilatedMotionVector_%u", (uint32)i);
			dilatedMotionVectorTextures[i]->setDebugName(debugName);
			
			dilatedMotionVectorSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(
				dilatedMotionVectorTextures[i].get(),
				ShaderResourceViewDesc{
					.format              = dilatedMotionVectorTextures[i]->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = 1,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					},
				}
			));
			dilatedMotionVectorUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				dilatedMotionVectorTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = dilatedMotionVectorTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}

	for (size_t i = 0; i < dilatedDepthTextures.size(); ++i)
	{
		if (dilatedDepthTextures[i] == nullptr
			|| dilatedDepthTextures[i]->getCreateParams().width != passInput.displaySizeX
			|| dilatedDepthTextures[i]->getCreateParams().height != passInput.displaySizeY)
		{
			commandList->enqueueDeferredDealloc(dilatedDepthTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(dilatedDepthSRVs[i].release(), true);
			commandList->enqueueDeferredDealloc(dilatedDepthUAVs[i].release(), true);

			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R32_FLOAT,
				// #wip: (FFX_RESOURCE_USAGE_RENDERTARGET | FFX_RESOURCE_USAGE_UAV | FFX_RESOURCE_USAGE_DCC_RENDERTARGET) ???
				ETextureAccessFlags::RTV | ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			dilatedDepthTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_DilatedDepth_%u", (uint32)i);
			dilatedDepthTextures[i]->setDebugName(debugName);
			
			dilatedDepthSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(
				dilatedDepthTextures[i].get(),
				ShaderResourceViewDesc{
					.format              = dilatedDepthTextures[i]->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = 1,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					},
				}
			));
			dilatedDepthUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				dilatedDepthTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = dilatedDepthTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}

	for (uint32 i = 0; i < 2; ++i)
	{
		if (gameMotionVectorFieldTextures[i] == nullptr
			|| gameMotionVectorFieldTextures[i]->getCreateParams().width != passInput.displaySizeX
			|| gameMotionVectorFieldTextures[i]->getCreateParams().height != passInput.displaySizeY)
		{
			commandList->enqueueDeferredDealloc(gameMotionVectorFieldTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(gameMotionVectorFieldUAVs[i].release(), true);

			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R32_UINT,
				ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			gameMotionVectorFieldTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_GameMotionVectorField_%s", i == 0 ? L"X" : L"Y");
			gameMotionVectorFieldTextures[i]->setDebugName(debugName);

			gameMotionVectorFieldUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				gameMotionVectorFieldTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = gameMotionVectorFieldTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}
}

void FrameGenPass::updateUniforms(RenderCommandList* commandList, const FrameGenPassInput& passInput)
{
	ConstantBufferView* frameInterpUniformCBV = getCurrentFrameInterpUniformCBV();
	ConstantBufferView* inpaintingPyramidUniformCBV = getCurrentInpaintingPyramidUniformCBV();

	FrameInterpUniform fiUniformData{
		.renderSize                 = { passInput.renderSizeX, passInput.renderSizeY },
		.displaySize                = { passInput.displaySizeX, passInput.displaySizeY },
		.displaySizeRcp             = { 1.0f / (float)(passInput.displaySizeX), 1.0f / (float)(passInput.displaySizeY) },
		.cameraNear                 = passInput.camera->getZNear(),
		.cameraFar                  = passInput.camera->getZFar(),
		.upscalerTargetSize         = { passInput.renderSizeX, passInput.renderSizeY }, // #wip: upscale target size
		.Mode                       = 0, // #wip: What is this? No shader accesses it, even ffx source code does not use it.
		.reset                      = 0, // #wip: reset, see ffxFrameInterpolationDispatch
		.fDeviceToViewDepth         = { 0, 0, 0, 0 }, // #wip: fDeviceToViewDepth, see setupDeviceDepthToViewSpaceDepthParams
		.deltaTime                  = passInput.deltaTime, // #wip: Unit of deltaTime?
		.HUDLessAttachedFactor      = 0,
		.distortionFieldSize        = { 1, 1 },
		.opticalFlowScale           = { 1.0f, 1.0f }, // #wip: opticalFlowScale
		.opticalFlowBlockSize       = kOpticalFlowBlockSize,
		.dispatchFlags              = passInput.dispatchFlags,
		.maxRenderSize              = { passInput.displaySizeX, passInput.displaySizeY },
		.opticalFlowHalfResMode     = 0, // #wip: opticalFlowHalfResMode
		.NumInstances               = 0, // #wip: NumInstances unused?
		.interpolationRectBase      = { 0, 0 },
		.interpolationRectSize      = { passInput.renderSizeX, passInput.renderSizeY },
		.debugBarColor              = { 1.0f, 0.0f, 0.0f },
		.backBufferTransferFunction = (uint32)passInput.backBufferTransferFunction,
		.minMaxLuminance            = { 0.0f, 65000.0f }, // #wip: minMaxLuminance
		.fTanHalfFOV                = 0.5f * std::tan(2.0f * std::atan(std::tan(passInput.camera->getFovYInRadians() * 0.5f) * passInput.camera->getAspectRatio())),
		._pad1                      = 0,
		.fJitter                    = { 0, 0 }, // #wip: jitter
		.fMotionVectorScale         = { 1.0f, 1.0f },
	};
	frameInterpUniformCBV->writeToGPU(commandList, &fiUniformData, sizeof(fiUniformData));

	uint32 dispatchThreadGroupCountXY[2];
	uint32 workGroupOffset[2];
	uint32 numWorkGroupsAndMips[2];
	uint32 rectInfo[4] = { 0, 0, (uint32)passInput.renderSizeX, (uint32)passInput.renderSizeY };
	ffxSpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo, -1);

	InpaintingPyramidUniform ipUniformData{
		.mips            = numWorkGroupsAndMips[1],
		.numWorkGroups   = numWorkGroupsAndMips[0],
		.workGroupOffset = { workGroupOffset[0], workGroupOffset[1] },
	};
	inpaintingPyramidUniformCBV->writeToGPU(commandList, &ipUniformData, sizeof(ipUniformData));
}

// See ffxFrameInterpolationPrepare().
// Generate FSR3 upscaler resources.
void FrameGenPass::preparePhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, FrameGenPrepare);

	const uint32 currResourceIndex = (cpuFrameIndex & 1);
	const uint32 prevResourceIndex = ((cpuFrameIndex + 1) & 1);

	auto uniformCBV = getCurrentFrameInterpUniformCBV();
	auto reconstructedPrevDepthTexture = reconstructedPrevDepthTextures[currResourceIndex].get();
	auto reconstructedPrevDepthUAV = reconstructedPrevDepthUAVs[currResourceIndex].get();
	auto dilatedMotionVectorTexture = dilatedMotionVectorTextures[currResourceIndex].get();
	auto dilatedMotionVectorUAV = dilatedMotionVectorUAVs[currResourceIndex].get();
	auto dilatedDepthTexture = dilatedDepthTextures[currResourceIndex].get();
	auto dilatedDepthUAV = dilatedDepthUAVs[currResourceIndex].get();

	// Clear estimated depth resources.
	auto fClearZero = ClearResourcePass::floatClearValue(getDeviceFarDepth(), 0, 0, 0);
	passInput.clearResourcePass->enqueueClear(reconstructedPrevDepthTexture, reconstructedPrevDepthUAV, fClearZero);
	passInput.clearResourcePass->executeClears(commandList, swapchainIndex);

	TextureBarrierAuto textureBarriers[] = {
		TextureBarrierAuto::toShaderResource(passInput.motionVectorTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.sceneDepthTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(reconstructedPrevDepthTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(dilatedMotionVectorTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(dilatedDepthTexture, EBarrierSync::COMPUTE_SHADING),
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	ShaderParameterTable SPT{};
	SPT.constantBuffer("cbFI", uniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
	SPT.texture("r_input_motion_vectors", passInput.motionVectorSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_INPUT_MOTION_VECTORS
	SPT.texture("r_input_depth", passInput.sceneDepthSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_INPUT_DEPTH
	SPT.rwTexture("rw_reconstructed_depth_previous_frame", reconstructedPrevDepthUAV); // FFX_FRAMEINTERPOLATION_BIND_UAV_RECONSTRUCTED_DEPTH_PREVIOUS_FRAME
	SPT.rwTexture("rw_dilated_motion_vectors", dilatedMotionVectorUAV); // FFX_FRAMEINTERPOLATION_BIND_UAV_DILATED_MOTION_VECTORS
	SPT.rwTexture("rw_dilated_depth", dilatedDepthUAV); // FFX_FRAMEINTERPOLATION_BIND_UAV_DILATED_DEPTH

	prepareDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());
	auto descriptorHeap = prepareDescriptor.getDescriptorHeap(swapchainIndex);
	auto pipeline = reconstructAndDilatePipeline.get();

	commandList->setComputePipelineState(pipeline);
	commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

	uint32 dispatchX = (passInput.renderSizeX + 7) / 8;
	uint32 dispatchY = (passInput.renderSizeY + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);
}

// See ffxFrameInterpolationDispatch.
void FrameGenPass::dispatchPhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	ConstantBufferView* frameInterpUniformCBV = getCurrentFrameInterpUniformCBV();
	ConstantBufferView* inpaintingPyramidUniformCBV = getCurrentInpaintingPyramidUniformCBV();

	const bool bReset = (interpolationDispatchCount == 0) || passInput.bReset;

	const bool bFrameID_decreased = passInput.frameID < prevFrameID;
	const bool bFrameID_skipped = (passInput.frameID - prevFrameID) > 1;
	const bool bDisjointFrameID = bFrameID_decreased || bFrameID_skipped;
	prevFrameID = passInput.frameID;
	interpolationDispatchCount++;

	const uint32 displayDispatchSizeX = (passInput.displaySizeX + 7) / 8;
	const uint32 displayDispatchSizeY = (passInput.displaySizeY + 7) / 8;

	const uint32 renderDispatchSizeX = (passInput.renderSizeX + 7) / 8;
	const uint32 renderDispatchSizeY = (passInput.renderSizeY + 7) / 8;

	const uint32 opticalFlowDispatchSizeX = (uint32)(passInput.displaySizeX / (float)kOpticalFlowBlockSize + 7) / 8;
	const uint32 opticalFlowDispatchSizeY = (uint32)(passInput.displaySizeY / (float)kOpticalFlowBlockSize + 7) / 8;

	// #wip: Dispatch setupPipeline
#if 0
	{
		SCOPED_DRAW_EVENT(commandList, Setup);

		// #wip: barrier
		TextureBarrierAuto textureBarriers[] = {
			TextureBarrierAuto::toShaderResource(passInput.opticalFlowPassOutput->sceneChangeDetectionTexture, EBarrierSync::COMPUTE_SHADING),
		};
		commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);
		
		// #wip: SPT
		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
		SPT.texture("r_optical_flow_scd", passInput.opticalFlowPassOutput->sceneChangeDetectionSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_OPTICAL_FLOW_SCENE_CHANGE_DETECTION
		SPT.rwTexture("rw_game_motion_vector_field_x", gameMotionVectorFieldUAVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_GAME_MOTION_VECTOR_FIELD_X
		SPT.rwTexture("rw_game_motion_vector_field_y", gameMotionVectorFieldUAVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_GAME_MOTION_VECTOR_FIELD_Y
		// "rw_optical_flow_motion_vector_field_x" FFX_FRAMEINTERPOLATION_BIND_UAV_OPTICAL_FLOW_MOTION_VECTOR_FIELD_X
		// "rw_optical_flow_motion_vector_field_y" FFX_FRAMEINTERPOLATION_BIND_UAV_OPTICAL_FLOW_MOTION_VECTOR_FIELD_Y
		// "rw_disocclusion_mask" FFX_FRAMEINTERPOLATION_BIND_UAV_DISOCCLUSION_MASK
		// "rw_counters" FFX_FRAMEINTERPOLATION_BIND_UAV_COUNTERS
		
		frameInterpDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());
		auto descriptorHeap = frameInterpDescriptor.getDescriptorHeap(swapchainIndex);
		auto pipeline = setupPipeline.get();

		commandList->setComputePipelineState(pipeline);
		commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);
		
		commandList->dispatchCompute(renderDispatchSizeX, renderDispatchSizeY, 1);
	}
#endif

	// #wip: Dispatch gameVectorFieldInpaintingPyramidPipeline

	if (bReset)
	{
		// #wip: Clear estimated depth resources
		// ...

		// #wip: Dispatch reconstructPrevDepthPipeline
		// #wip: Dispatch gameMotionVectorFieldPipeline
		
		// #wip: scheduleDispatchGameVectorFieldInpaintingPyramid()

		// #wip: Dispatch opticalFlowVectorFieldPipeline
		// #wip: Dispatch disocclusionMaskPipeline
	}

	// #wip: Dispatch interpolationPipeline (pipelineFiScfi in ffx)

	// inpainting pyramid
	{
		// #wip: Auto exposure via SPD
		// #wip: Dispatch inpaintingPyramidPipeline
	}

	// #wip: inpaintingPipeline

	if (false /* draw debug view */)
	{
		// #wip: Dispatch debugViewPipeline
	}

	// #wip: Store current buffer
}

ConstantBufferView* FrameGenPass::getCurrentFrameInterpUniformCBV()
{
	return frameInterpDescriptor.getUniformCBV(cpuFrameIndex & 1);
}

ConstantBufferView* FrameGenPass::getCurrentInpaintingPyramidUniformCBV()
{
	return inpaintingPyramidDescriptor.getUniformCBV(cpuFrameIndex & 1);
}
