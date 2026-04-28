#include "frame_gen_pass.h"
#include "rhi/render_device.h"
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

	initializePipelines();
}

void FrameGenPass::runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	updateUniforms(commandList, passInput);
	//preparePhase(commandList, swapchainIndex, passInput);
	//dispatchPhase(commandList, swapchainIndex, passInput);

	cpuFrameIndex += 1;
}

void FrameGenPass::initializePipelines()
{
	const uint32 swapchainCount = 2;//device->maxFramesInFlight(); // Always need 2

	frameInterpDescriptor.initialize(L"FSR3_FrameInterpUniform", swapchainCount, sizeof(FrameInterpUniform));
	inpaintingPyramidDescriptor.initialize(L"FSR3_InpaintingPyramidUniform", swapchainCount, sizeof(InpaintingPyramidUniform));

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
	// #wip: Dispatch reconstructAndDilatePipeline
	// Texture2D<float4> r_input_motion_vectors (FFX_FRAMEINTERPOLATION_BIND_SRV_INPUT_MOTION_VECTORS)
	// Texture2D<float> r_input_depth (FFX_FRAMEINTERPOLATION_BIND_SRV_INPUT_DEPTH)
	// RWTexture2D<uint> rw_reconstructed_depth_previous_frame (FFX_FRAMEINTERPOLATION_BIND_UAV_RECONSTRUCTED_DEPTH_PREVIOUS_FRAME)
	// RWTexture2D<float2> rw_dilated_motion_vectors (FFX_FRAMEINTERPOLATION_BIND_UAV_DILATED_MOTION_VECTORS)
	// RWTexture2D<float> rw_dilated_depth (FFX_FRAMEINTERPOLATION_BIND_UAV_DILATED_DEPTH)
	// cbuffer cbFI (FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION)
}

// See ffxFrameInterpolationDispatch.
void FrameGenPass::dispatchPhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	ConstantBufferView* frameInterpUniformCBV = getCurrentFrameInterpUniformCBV();
	ConstantBufferView* inpaintingPyramidUniformCBV = getCurrentInpaintingPyramidUniformCBV();

	const uint32 displayDispatchSizeX = (passInput.displaySizeX + 7) / 8;
	const uint32 displayDispatchSizeY = (passInput.displaySizeY + 7) / 8;

	const uint32 renderDispatchSizeX = (passInput.renderSizeX + 7) / 8;
	const uint32 renderDispatchSizeY = (passInput.renderSizeY + 7) / 8;

	const uint32 opticalFlowDispatchSizeX = (uint32)(passInput.displaySizeX / (float)kOpticalFlowBlockSize + 7) / 8;
	const uint32 opticalFlowDispatchSizeY = (uint32)(passInput.displaySizeY / (float)kOpticalFlowBlockSize + 7) / 8;

	// #wip: Dispatch setupPipeline (renderDispatchSizeX/Y)
	// #wip: Dispatch gameVectorFieldInpaintingPyramidPipeline

	if (passInput.bReset)
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
	return frameInterpDescriptor.getUniformCBV(cpuFrameIndex);
}

ConstantBufferView* FrameGenPass::getCurrentInpaintingPyramidUniformCBV()
{
	return inpaintingPyramidDescriptor.getUniformCBV(cpuFrameIndex);
}
