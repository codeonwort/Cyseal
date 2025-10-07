#include "hiz_pass.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"

void HiZPass::initialize()
{
	RenderDevice* device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	copyPassDescriptor.initialize(L"HiZ_CopyMip0Pass", swapchainCount, 0);
	downsamplePassDescriptor.initialize(L"HiZ_DownsamplePass", swapchainCount, 0);

	ShaderStage* copyShader = device->createShader(EShaderStage::COMPUTE_SHADER, "HiZCopyMip0CS");
	copyShader->declarePushConstants({ { "pushConstants", 3 } });
	copyShader->loadFromFile(L"hiz.hlsl", "copyMip0CS");

	ShaderStage* downsampleShader = device->createShader(EShaderStage::COMPUTE_SHADER, "HiZDownsampleCS");
	downsampleShader->declarePushConstants({ { "pushConstants", 3} });
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
	// sceneDepth is in PIXEL_SHADER_RESOURCE state.
	// Currently all mips of HiZ are in UNORDERED_ACCESS state.

	// Copy mip0.
	{
		uint32 packedSize = Cymath::packUint16x2(passInput.textureWidth, passInput.textureHeight);

		ShaderParameterTable SPT{};
		SPT.pushConstants("pushConstants", { packedSize, packedSize, 0 }, 0);
		SPT.texture("inputTexture", passInput.sceneDepthSRV);
		SPT.rwTexture("outputTexture", passInput.hizUAVs.at(0));

		// Resize volatile heaps if needed.
		uint32 requiredVolatiles = SPT.totalDescriptors();
		copyPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

		commandList->setComputePipelineState(copyPipeline.get());

		DescriptorHeap* volatileHeap = copyPassDescriptor.getDescriptorHeap(swapchainIndex);
		commandList->bindComputeShaderParameters(copyPipeline.get(), &SPT, volatileHeap);

		uint32 dispatchX = (passInput.textureWidth + 7) / 8;
		uint32 dispatchY = (passInput.textureHeight + 7) / 8;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		TextureBarrierAuto texBarriers[] = {
			{
				EBarrierSync::DEPTH_STENCIL, EBarrierAccess::DEPTH_STENCIL_READ, EBarrierLayout::DepthStencilRead,
				passInput.sceneDepthTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				passInput.hizTexture, BarrierSubresourceRange::singleMip(0), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(texBarriers), texBarriers, 0, nullptr);
	}

	uint32 mipCount = (uint32)passInput.hizUAVs.size();
	uint32 prevWidth = passInput.textureWidth;
	uint32 prevHeight = passInput.textureHeight;
	DescriptorIndexTracker tracker;

	for (uint32 currMip = 1; currMip < mipCount; ++currMip)
	{
		uint32 currWidth = max(1, prevWidth / 2);
		uint32 currHeight = max(1, prevHeight / 2);

		uint32 packedInputSize = Cymath::packUint16x2(prevWidth, prevHeight);
		uint32 packedOutputSize = Cymath::packUint16x2(currWidth, currHeight);

		ShaderParameterTable SPT{};
		SPT.pushConstants("pushConstants", { packedInputSize, packedOutputSize, currMip }, 0);
		SPT.texture("inputTexture", passInput.hizSRV); // #todo-rhi: Is it ok to bind SRV that covers all mips?
		SPT.rwTexture("outputTexture", passInput.hizUAVs.at(currMip));

		// Resize volatile heaps if needed.
		uint32 requiredVolatiles = SPT.totalDescriptors();
		downsamplePassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles * mipCount);

		commandList->setComputePipelineState(downsamplePipeline.get());

		DescriptorHeap* volatileHeap = downsamplePassDescriptor.getDescriptorHeap(swapchainIndex);
		commandList->bindComputeShaderParameters(downsamplePipeline.get(), &SPT, volatileHeap, &tracker);

		uint32 dispatchX = (currWidth + 7) / 8;
		uint32 dispatchY = (currHeight + 7) / 8;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		TextureBarrierAuto texBarriers[] = {
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				passInput.hizTexture, BarrierSubresourceRange::singleMip(currMip), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(texBarriers), texBarriers, 0, nullptr);

		prevWidth = currWidth;
		prevHeight = currHeight;
	}

	// From now on, all mips of HiZ are in PIXEL_SHADER_RESOURCE state.
}
