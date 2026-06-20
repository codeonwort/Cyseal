#include "bilateral_blur.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"

// #todo-renderer: scratch texture format
#define PF_scratch  EPixelFormat::R16G16B16A16_FLOAT
#define PF_variance EPixelFormat::R16_FLOAT

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

void BilateralBlur::initialize(RenderDevice* inDevice)
{
	device = inDevice;
	const uint32 maxFramesInFlight = device->maxFramesInFlight();

	initPassDescriptor.initialize(L"BilateralBlurInit", maxFramesInFlight, 0);
	passDescriptor.initialize(L"BilateralBlur", maxFramesInFlight, sizeof(BlurUniform));

	// Init pipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "BilateralBlurInitCS");
		shader->declarePushConstants({ { "pushConstants", 2 } });
		shader->loadFromFile(L"bilateral_blur_init.hlsl", "mainCS");

		initPipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0 }
		));

		delete shader;
	}

	// Blur pipeline
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "BilateralBlurCS");
		shader->declarePushConstants({ { "pushConstants", 1} });
		shader->loadFromFile(L"bilateral_blur.hlsl", "mainCS");

		pipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0 }
		));

		delete shader;
	}
}

void BilateralBlur::renderBilateralBlur(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, BilateralBlur);

	resizeTexture(commandList, passInput.imageWidth, passInput.imageHeight);

	initPhase(commandList, frameInfo, passInput);
	blurPhase(commandList, frameInfo, passInput);
}

void BilateralBlur::resizeTexture(RenderCommandList* commandList, uint32 width, uint32 height)
{
	auto nullOrWrongSize = [](const UniquePtr<Texture>& texture, uint32 width, uint32 height) -> bool {
		return texture == nullptr
			|| texture->getCreateParams().width != width
			|| texture->getCreateParams().height != height;
	};

	auto createSingleMipUAV = [device = device](Texture* texture, uint32 targetMip) -> UniquePtr<UnorderedAccessView> {
		return UniquePtr<UnorderedAccessView>(device->createUAV(texture,
			UnorderedAccessViewDesc{
				.format         = texture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = targetMip, .planeSlice = 0 },
			}
		));
	};

	if (nullOrWrongSize(colorScratch, width, height))
	{
		commandList->enqueueDeferredDealloc(colorScratch.release(), true);
		commandList->enqueueDeferredDealloc(colorScratchUAV.release(), true);

		const TextureCreateParams colorDesc = TextureCreateParams::texture2D(
			PF_scratch, ETextureAccessFlags::UAV, width, height, 1, 1, 0);

		colorScratch = UniquePtr<Texture>(device->createTexture(colorDesc));
		colorScratch->setDebugName(L"RT_BilateralBlurColorScratch");

		colorScratchUAV = createSingleMipUAV(colorScratch.get(), 0);
	}

	for (uint32 i = 0; i < 2; ++i)
	{
		if (nullOrWrongSize(varianceTextures[i], width, height))
		{
			commandList->enqueueDeferredDealloc(varianceTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(varianceUAVs[i].release(), true);

			const TextureCreateParams texDesc = TextureCreateParams::texture2D(
				PF_variance, ETextureAccessFlags::UAV, width, height);

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_Variance_%u", i);

			varianceTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));
			varianceTextures[i]->setDebugName(debugName);

			varianceUAVs[i] = createSingleMipUAV(varianceTextures[i].get(), 0);
		}
	}
}

void BilateralBlur::initPhase(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, BilateralBlurInit);

	const uint32 currFrame = frameInfo.frameID % 2;
	const uint32 prevFrame = (frameInfo.frameID + 1) % 2;

	auto varianceTexture = varianceTextures[currFrame].get();
	auto varianceUAV = varianceUAVs[currFrame].get();

	TextureBarrierAuto textureBarriers[] = {
		TextureBarrierAuto::toShaderResource(passInput.inMomentTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(varianceTexture, EBarrierSync::COMPUTE_SHADING),
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	ShaderParameterTable SPT{};
	SPT.pushConstants("pushConstants", { passInput.imageWidth, passInput.imageHeight });
	SPT.texture("momentTexture", passInput.inMomentSRV);
	SPT.rwTexture("rwVarianceTexture", varianceUAV);

	auto pipelineState = initPipelineState.get();
	auto descriptorHeap = initPassDescriptor.resizeDescriptorHeap(frameInfo, SPT.totalDescriptors());

	commandList->setComputePipelineState(pipelineState);
	commandList->bindComputeShaderParameters(pipelineState, &SPT, descriptorHeap);

	uint32 dispatchX = (passInput.imageWidth + 7) / 8;
	uint32 dispatchY = (passInput.imageHeight + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);
}

void BilateralBlur::blurPhase(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, BilateralBlurIteration);

	CHECK(passInput.blurCount > 0);
	const bool bInOutColorsAreSame = passInput.inColorTexture == passInput.outColorTexture;

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

		passDescriptor.resizeDescriptorHeap(frameInfo, requiredVolatiles * passInput.blurCount);
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

		auto uniformCBV = passDescriptor.getUniformCBV(frameInfo);
		uniformCBV->writeToGPU(commandList, &uboData, sizeof(BlurUniform));
	}

	commandList->setComputePipelineState(pipelineState.get());

	// Bind shader parameters.
	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(frameInfo);
	ConstantBufferView* uniformCBV = passDescriptor.getUniformCBV(frameInfo);
	DescriptorIndexTracker tracker;
	Texture* blurInputTexture = passInput.inColorTexture;
	UnorderedAccessView* blurInputUAV = passInput.inColorUAV;
	Texture* blurOutputTexture = colorScratch.get();
	UnorderedAccessView* blurOutputUAV = colorScratchUAV.get();

	std::vector<Texture*> uavBarrierTargets = { passInput.inColorTexture, colorScratch.get() };
	if (!bInOutColorsAreSame) uavBarrierTargets.push_back(passInput.outColorTexture);
	std::vector<TextureBarrierAuto> uavBarriers;
	for (size_t i = 0; i < uavBarrierTargets.size(); ++i)
	{
		uavBarriers.push_back(TextureBarrierAuto::toUnorderedAccess(uavBarrierTargets[i], EBarrierSync::COMPUTE_SHADING));
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
				blurOutputTexture = passInput.outColorTexture;
				blurOutputUAV = passInput.outColorUAV;
			}
		}

		ShaderParameterTable SPT{};
		SPT.pushConstant("pushConstants", phase + 1);
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformCBV);
		SPT.constantBuffer("blurUniform", uniformCBV);
		SPT.rwTexture("inColorTexture", blurInputUAV);
		SPT.texture("inGBuffer0Texture", passInput.inGBuffer0SRV);
		SPT.texture("inGBuffer1Texture", passInput.inGBuffer1SRV);
		SPT.texture("inDepthTexture", passInput.inSceneDepthSRV);
		SPT.rwTexture("outputTexture", blurOutputUAV);

		commandList->bindComputeShaderParameters(pipelineState.get(), &SPT, volatileHeap, &tracker);

		uint32 groupX = (passInput.imageWidth + 7) / 8, groupY = (passInput.imageHeight + 7) / 8;
		commandList->dispatchCompute(groupX, groupY, 1);

		commandList->barrierAuto(0, nullptr, (uint32)uavBarriers.size(), uavBarriers.data(), 0, nullptr);

		if (bInOutColorsAreSame == false && phase + 1 == passInput.feedbackPhase)
		{
			TextureBarrierAuto barriersBefore[] = {
				TextureBarrierAuto::toCopySource(blurOutputTexture),
				TextureBarrierAuto::toCopyDest(passInput.inColorTexture),
			};
			commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

			commandList->copyTexture2D(blurOutputTexture, passInput.inColorTexture);

			TextureBarrierAuto barriersAfter[] = {
				TextureBarrierAuto::toUnorderedAccess(blurOutputTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toUnorderedAccess(passInput.inColorTexture, EBarrierSync::COMPUTE_SHADING),
			};
			commandList->barrierAuto(0, nullptr, _countof(barriersAfter), barriersAfter, 0, nullptr);
		}

		auto tempTex = blurInputTexture; auto tempUAV = blurInputUAV;
		blurInputTexture = blurOutputTexture; blurInputUAV = blurOutputUAV;
		blurOutputTexture = tempTex; blurOutputUAV = tempUAV;
	}

	if (bShouldCopyScratchToOutColor)
	{
		TextureBarrierAuto barriersBefore[] = {
			TextureBarrierAuto::toCopySource(colorScratch.get()),
			TextureBarrierAuto::toCopyDest(passInput.outColorTexture),
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		commandList->copyTexture2D(colorScratch.get(), passInput.outColorTexture);
	}
}
