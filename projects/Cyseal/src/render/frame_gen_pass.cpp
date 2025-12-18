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
}
