#include "bilateral_blur.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"

struct BlurUniform
{
	float kernelAndOffset[4 * 25];
	float cPhi;
	float nPhi;
	float pPhi;
	float _pad0;
	uint32 textureWidth;
	uint32 textureHeight;
	uint32 bSkipBlur;
	uint32 _pad2;
};

void BilateralBlur::initialize()
{
	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	// Blur pipeline
	{
		ShaderStage* shader = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "BilateralBlurCS");
		shader->declarePushConstants({ { "pushConstants", 1} });
		shader->loadFromFile(L"bilateral_blur.hlsl", "mainCS");

		pipelineState = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0 }
		));

		delete shader;
	}

	passDescriptor.initialize(L"BilateralBlur", swapchainCount, sizeof(BlurUniform));
}

void BilateralBlur::renderBilateralBlur(RenderCommandList* commandList, uint32 swapchainIndex, const BilateralBlurInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, BilateralBlur);

	CHECK(passInput.blurCount > 0);
	const bool bInOutColorsAreSame = passInput.inColorTexture == passInput.outColorTexture;

	resizeTexture(commandList, passInput.imageWidth, passInput.imageHeight);

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // pushConstants
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // blurUniform
		requiredVolatiles += 1; // inColorTexture
		requiredVolatiles += 2; // inGBuffer0Texture, inGBuffer1Texture
		requiredVolatiles += 1; // inDepthTexture
		requiredVolatiles += 1; // outputTexture

		passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles * passInput.blurCount);
	}

	// Update uniforms.
	{
		BlurUniform uboData;
		int32 k = 0;
		float kernel1D[3] = { 1.0f, 2.0f / 3.0f, 1.0f / 6.0f };
		for (int32 y = -2; y <= 2; ++y)
		{
			for (int32 x = -2; x <= 2; ++x)
			{
				uboData.kernelAndOffset[k * 4 + 0] = kernel1D[std::abs(x)] * kernel1D[std::abs(y)];
				uboData.kernelAndOffset[k * 4 + 1] = (float)x;
				uboData.kernelAndOffset[k * 4 + 2] = (float)y;
				uboData.kernelAndOffset[k * 4 + 3] = 0.0f;
				++k;
			}
		}
		uboData.cPhi = passInput.cPhi;
		uboData.nPhi = passInput.nPhi;
		uboData.pPhi = passInput.pPhi;
		uboData.textureWidth = passInput.imageWidth;
		uboData.textureHeight = passInput.imageHeight;
		uboData.bSkipBlur = (uint32)false;

		auto uniformCBV = passDescriptor.getUniformCBV(swapchainIndex);
		uniformCBV->writeToGPU(commandList, &uboData, sizeof(BlurUniform));
	}

	commandList->setComputePipelineState(pipelineState.get());

	// Bind shader parameters.
	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);
	ConstantBufferView* uniformCBV = passDescriptor.getUniformCBV(swapchainIndex);
	DescriptorIndexTracker tracker;
	UnorderedAccessView* blurInput = passInput.inColorUAV;
	UnorderedAccessView* blurOutput = colorScratchUAV.get();

	std::vector<Texture*> uavBarrierTargets = { passInput.inColorTexture, colorScratch.get() };
	if (!bInOutColorsAreSame) uavBarrierTargets.push_back(passInput.outColorTexture);
	std::vector<TextureBarrierAuto> uavBarriers;
	for (size_t i = 0; i < uavBarrierTargets.size(); ++i)
	{
		uavBarriers.push_back({
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			uavBarrierTargets[i], BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
		});
	}

	bool bShouldCopyScratchToOutColor = false;
	for (int32 phase = 0; phase < passInput.blurCount; ++phase)
	{
		if (phase == passInput.blurCount - 1)
		{
			if (passInput.blurCount == 1 && bInOutColorsAreSame)
			{
				bShouldCopyScratchToOutColor = true;
			}
			else
			{
				blurOutput = passInput.outColorUAV;
			}
		}

		ShaderParameterTable SPT{};
		SPT.pushConstant("pushConstants", phase + 1);
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformCBV);
		SPT.constantBuffer("blurUniform", uniformCBV);
		SPT.rwTexture("inColorTexture", blurInput);
		SPT.texture("inGBuffer0Texture", passInput.inGBuffer0SRV);
		SPT.texture("inGBuffer1Texture", passInput.inGBuffer1SRV);
		SPT.texture("inDepthTexture", passInput.inSceneDepthSRV);
		SPT.rwTexture("outputTexture", blurOutput);

		commandList->bindComputeShaderParameters(pipelineState.get(), &SPT, volatileHeap, &tracker);

		uint32 groupX = (passInput.imageWidth + 7) / 8, groupY = (passInput.imageHeight + 7) / 8;
		commandList->dispatchCompute(groupX, groupY, 1);

		commandList->barrierAuto(0, nullptr, (uint32)uavBarriers.size(), uavBarriers.data(), 0, nullptr);

		auto temp = blurInput;
		blurInput = blurOutput;
		blurOutput = temp;
	}

	if (bShouldCopyScratchToOutColor)
	{
		TextureBarrierAuto barriersBefore[] = {
			{
				EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, EBarrierLayout::CopySource,
				colorScratch.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COPY, EBarrierAccess::COPY_DEST, EBarrierLayout::CopyDest,
				passInput.outColorTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		commandList->copyTexture2D(colorScratch.get(), passInput.outColorTexture);
	}
}

void BilateralBlur::resizeTexture(RenderCommandList* commandList, uint32 width, uint32 height)
{
	if (colorScratch != nullptr && colorScratch->getCreateParams().width == width && colorScratch->getCreateParams().height == height)
	{
		return;
	}

	const TextureCreateParams colorDesc = TextureCreateParams::texture2D(
		EPixelFormat::R32G32B32A32_FLOAT, ETextureAccessFlags::UAV, width, height, 1, 1, 0);

	commandList->enqueueDeferredDealloc(colorScratch.release(), true);

	colorScratch = UniquePtr<Texture>(gRenderDevice->createTexture(colorDesc));
	colorScratch->setDebugName(L"RT_BilateralBlurColorScratch");

	colorScratchUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(colorScratch.get(),
		UnorderedAccessViewDesc{
			.format        = colorDesc.format,
			.viewDimension = EUAVDimension::Texture2D,
			.texture2D     = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));
}
