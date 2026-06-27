#include "bilateral_blur.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"

// #todo-renderer: scratch texture format
#define PF_scratch  EPixelFormat::R16G16B16A16_FLOAT
#define PF_variance EPixelFormat::R16_FLOAT

// This is not the max invocation count of renderBilateralBlur() per frame, probably less than that.
// Each renderBilateralBlur() call requires N blur count, where N is given by BilateralBlurInput::blurCount.
#define MAX_BLUR_COUNT_PER_FRAME 256

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
	uint32 lastPhase;
};

void BilateralBlur::initialize(RenderDevice* inDevice)
{
	device = inDevice;
	maxBlursPerFrame = MAX_BLUR_COUNT_PER_FRAME;

	const uint32 maxFramesInFlight = device->maxFramesInFlight();

	initPassDescriptor.initialize(L"BilateralBlurInit", maxFramesInFlight, 0);
	blurPassDescriptor.initialize(L"BilateralBlur", maxFramesInFlight, sizeof(BlurUniform));

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
		shader->declarePushConstants({ { "pushConstants", 2} });
		shader->loadFromFile(L"bilateral_blur.hlsl", "mainCS");

		blurPipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0 }
		));

		delete shader;
	}
}

void BilateralBlur::resetPerFrameResources(const FrameInfo& frameInfo)
{
	initDescriptorTracker.reset();
	blurDescriptorTracker.reset();
	currentNumBlurs = 0;

	// #todo-rhi: Now it's distant from actual SPT setup, more error prone.
	uint32 requiredVolatiles = 0;
	requiredVolatiles += 1; // pushConstants
	requiredVolatiles += 1; // momentTexture
	requiredVolatiles += 1; // rwVarianceTexture
	initPassDescriptor.resizeDescriptorHeap(frameInfo, requiredVolatiles * maxBlursPerFrame);

	requiredVolatiles = 0;
	requiredVolatiles += 1; // pushConstants
	requiredVolatiles += 1; // sceneUniform
	requiredVolatiles += 1; // blurUniform
	requiredVolatiles += 3; // inDepthTexture, inGBuffer0Texture, inGBuffer1Texture
	requiredVolatiles += 2; // inColorTexture, outColorTexture
	requiredVolatiles += 2; // inVarianceTexture, outVarianceTexture
	blurPassDescriptor.resizeDescriptorHeap(frameInfo, requiredVolatiles * maxBlursPerFrame);
}

void BilateralBlur::renderBilateralBlur(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, BilateralBlur);

	CHECK(currentNumBlurs + (uint32)passInput.blurCount <= maxBlursPerFrame);
	currentNumBlurs += (uint32)passInput.blurCount;

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

	// When modified, check resetPerFrameResources() if SPT size is correct.
	ShaderParameterTable SPT{};
	SPT.pushConstants("pushConstants", { passInput.imageWidth, passInput.imageHeight });
	SPT.texture("momentTexture", passInput.inMomentSRV);
	SPT.rwTexture("rwVarianceTexture", varianceUAV);

	auto pipelineState = initPipelineState.get();
	auto descriptorHeap = initPassDescriptor.getDescriptorHeap(frameInfo);

	commandList->setComputePipelineState(pipelineState);
	commandList->bindComputeShaderParameters(pipelineState, &SPT, descriptorHeap, &initDescriptorTracker);

	uint32 dispatchX = (passInput.imageWidth + 7) / 8;
	uint32 dispatchY = (passInput.imageHeight + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);
}

void BilateralBlur::blurPhase(RenderCommandList* commandList, const FrameInfo& frameInfo, const BilateralBlurInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, BilateralBlurIteration);

	CHECK(passInput.blurCount > 0);
	const bool bInOutColorsAreSame = passInput.inColorTexture == passInput.outColorTexture;

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
		uboData.lastPhase = (uint32)(passInput.blurCount - 1);

		auto uniformCBV = blurPassDescriptor.getUniformCBV(frameInfo);
		uniformCBV->writeToGPU(commandList, &uboData, sizeof(BlurUniform));
	}

	commandList->setComputePipelineState(blurPipelineState.get());

	// Bind shader parameters.
	DescriptorHeap* volatileHeap   = blurPassDescriptor.getDescriptorHeap(frameInfo);
	ConstantBufferView* uniformCBV = blurPassDescriptor.getUniformCBV(frameInfo);

	Texture*             blurInputTexture   = passInput.inColorTexture;
	UnorderedAccessView* blurInputUAV       = passInput.inColorUAV;
	Texture*             blurOutputTexture  = colorScratch.get();
	UnorderedAccessView* blurOutputUAV      = colorScratchUAV.get();

	const uint32         currFrame          = frameInfo.frameID % 2;
	const uint32         prevFrame          = (frameInfo.frameID + 1) % 2;
	Texture*             inVarianceTexture  = varianceTextures[currFrame].get();
	UnorderedAccessView* inVarianceUAV      = varianceUAVs[currFrame].get();
	Texture*             outVarianceTexture = varianceTextures[prevFrame].get();
	UnorderedAccessView* outVarianceUAV     = varianceUAVs[prevFrame].get();

	// Resources to transition to UnorderedAccess layout before dispatching shaders.
	std::vector<Texture*> uavBarrierTargets = {
		passInput.inColorTexture, colorScratch.get(),
		inVarianceTexture, outVarianceTexture,
	};
	if (!bInOutColorsAreSame) uavBarrierTargets.push_back(passInput.outColorTexture);

	std::vector<TextureBarrierAuto> uavBarriers;
	for (size_t i = 0; i < uavBarrierTargets.size(); ++i)
	{
		uavBarriers.push_back(TextureBarrierAuto::toUnorderedAccess(uavBarrierTargets[i], EBarrierSync::COMPUTE_SHADING));
	}
	commandList->barrierAuto(0, nullptr, (uint32)uavBarriers.size(), uavBarriers.data(), 0, nullptr);

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

		// When modified, check resetPerFrameResources() if SPT size is correct.
		ShaderParameterTable SPT{};
		SPT.pushConstants("pushConstants", { (uint32)(phase + 1), (uint32)phase });
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformCBV);
		SPT.constantBuffer("blurUniform", uniformCBV);
		SPT.texture("inGBuffer0Texture", passInput.inGBuffer0SRV);
		SPT.texture("inGBuffer1Texture", passInput.inGBuffer1SRV);
		SPT.texture("inDepthTexture", passInput.inSceneDepthSRV);
		SPT.rwTexture("inColorTexture", blurInputUAV);
		SPT.rwTexture("inVarianceTexture", inVarianceUAV);
		SPT.rwTexture("outColorTexture", blurOutputUAV);
		SPT.rwTexture("outVarianceTexture", outVarianceUAV);

		commandList->bindComputeShaderParameters(blurPipelineState.get(), &SPT, volatileHeap, &blurDescriptorTracker);

		uint32 groupX = (passInput.imageWidth + 7) / 8, groupY = (passInput.imageHeight + 7) / 8;
		commandList->dispatchCompute(groupX, groupY, 1);

		// All UAVs need to be finalized, so just issue a global barrier.
		GlobalBarrier globalBarrier{
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING,
			EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS,
		};
		commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);

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

		tempTex = inVarianceTexture; tempUAV = inVarianceUAV;
		inVarianceTexture = outVarianceTexture; inVarianceUAV = outVarianceUAV;
		outVarianceTexture = tempTex; outVarianceUAV = tempUAV;
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
