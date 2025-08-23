#include "hiz_pass.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"

// Assumes width < 65536 and height < 65536.
static uint32 packUint32x2(uint32 width, uint32 height)
{
	return ((width & 0xffff) << 16) | (height & 0xffff);
}

void HiZPass::initialize()
{
	RenderDevice* device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	copyPassDescriptor.initialize(L"HiZ_CopyMip0Pass", swapchainCount, 0);
	downsamplePassDescriptor.initialize(L"HiZ_DownsamplePass", swapchainCount, 0);

	ShaderStage* copyShader = device->createShader(EShaderStage::COMPUTE_SHADER, "HiZCopyMip0CS");
	copyShader->declarePushConstants({ "pushConstants" });
	copyShader->loadFromFile(L"hiz.hlsl", "copyMip0CS");

	ShaderStage* downsampleShader = device->createShader(EShaderStage::COMPUTE_SHADER, "HiZDownsampleCS");
	downsampleShader->declarePushConstants({ "pushConstants" });
	downsampleShader->loadFromFile(L"hiz.hlsl", "downsampleCS");

	copyPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
		ComputePipelineDesc{ .cs = copyShader, .nodeMask = 0, }
	));
	downsamplePipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
		ComputePipelineDesc{ .cs = downsampleShader, .nodeMask = 0, }
	));

	// No use after PSO creation.
	delete copyShader;
	delete downsampleShader;
}

void HiZPass::renderHiZ(RenderCommandList* commandList, uint32 swapchainIndex, const HiZPassInput& passInput)
{
#if 0
	// Copy mip0.
	{
		uint32 packedSize = packUint32x2(passInput.textureWidth, passInput.textureHeight);

		ShaderParameterTable SPT{};
		// #wip: Apprently this is not the way? Need to support SetComputeRoot32BitConstants().
		SPT.pushConstant("pushConstants", packedSize, 0 /* packedInputSize */);
		SPT.pushConstant("pushConstants", packedSize, 1 /* packedOutputSize */);
		SPT.texture("inputTexture", passInput.sceneDepthSRV);
		SPT.rwTexture("outputTexture", passInput.hizUAVs.at(0));

		// Resize volatile heaps if needed.
		uint32 requiredVolatiles = SPT.totalDescriptors();
		copyPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

		DescriptorHeap* volatileHeap = copyPassDescriptor.getDescriptorHeap(swapchainIndex);
		commandList->bindComputeShaderParameters(copyPipeline.get(), &SPT, volatileHeap);

		uint32 dispatchX = (passInput.textureWidth + 7) / 8;
		uint32 dispatchY = (passInput.textureHeight + 7) / 8;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);
	}
#endif
}
