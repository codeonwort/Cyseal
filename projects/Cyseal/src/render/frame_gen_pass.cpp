#include "frame_gen_pass.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"

void FrameGenPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;

	initializePipelines();
}

void FrameGenPass::runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	//
}

void FrameGenPass::initializePipelines()
{
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	// reconstructAndDilatePipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3ReconstructAndDilateCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_reconstruct_and_dilate_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		reconstructAndDilatePipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// setupPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3SetupCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_setup_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		setupPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// reconstructPrevDepthPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3ReconstructPrevDepthCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_reconstruct_previous_depth_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		reconstructPrevDepthPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// gameMotionVectorFieldPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3GameMotionVectorFieldPipelineCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_game_motion_vector_field_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		gameMotionVectorFieldPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// opticalFlowVectorFieldPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3OpticalFlowVectorFieldPipelineCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_optical_flow_vector_field_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		opticalFlowVectorFieldPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// disocclusionMaskPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3DisocclusionMaskPipelineCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_disocclusion_mask_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		disocclusionMaskPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// interpolationPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3InterpolationPipelineCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		interpolationPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// inpaintingPyramidPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3InpaintingPyramidPipelineCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_compute_inpainting_pyramid_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		inpaintingPyramidPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// inpaintingPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3InpaintingPipelineCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_inpainting_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		inpaintingPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// gameVectorFieldInpaintingPyramidPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3GameVectorFieldInpaintingPyramidPipelineCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		gameVectorFieldInpaintingPyramidPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}

	// debugViewPipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "FSR3DebugViewPipelineCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_frameinterpolation_debug_view_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		debugViewPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs       = shader,
				.nodeMask = 0,
			}
		));

		delete shader;
	}
}
