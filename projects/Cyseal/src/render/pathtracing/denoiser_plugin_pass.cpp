#include "denoiser_plugin_pass.h"

#include "rhi/render_device.h"
#include "rhi/denoiser_device.h"
#include "rhi/render_command.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/shader.h"

DEFINE_LOG_CATEGORY_STATIC(LogDenoiserPlugin);

// We will denoise only RGB but include A channel for UAV.
// Should provide proper stride so that the plugin only consider RGB channels.
static constexpr EPixelFormat DENOISER_INPUT_FORMAT = EPixelFormat::R32G32B32A32_FLOAT;

void DenoiserPluginPass::initialize(RenderDevice* inDevice)
{
	device = inDevice;

	if (!isAvailable())
	{
		CYLOG(LogDenoiserPlugin, Warning, L"Denoiser device is unavailable. Denoiser pass will be disabled.");
		return;
	}

	// Shader
	{
		ShaderStage* blitShader = device->createShader(EShaderStage::COMPUTE_SHADER, "BlitDenoiserInputCS");
		blitShader->declarePushConstants({ { "pushConstants", 1} });
		blitShader->loadFromFile(L"blit_denoiser_input.hlsl", "mainCS");

		blitPipelineState = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = blitShader, .nodeMask = 0 }
		));

		delete blitShader;
	}

	const uint32 swapchainCount = device->maxFramesInFlight();
	blitPassDescriptor.initialize(L"DenoiserPlugin_BlitPass", swapchainCount, 0);
}

bool DenoiserPluginPass::isAvailable() const
{
	return device->getDenoiserDevice()->isValid();
}

void DenoiserPluginPass::blitTextures(RenderCommandList* commandList, uint32 swapchainIndex, const DenoiserPluginInput& passInput)
{
	const uint32 width = passInput.imageWidth;
	const uint32 height = passInput.imageHeight;
	CHECK(width < 0xFFFF && height < 0xFFFF);
	const uint32 packedWidthHeight = ((width & 0xFFFF) << 16) | (height & 0xFFFF);

	resizeTextures(width, height);

	{
		TextureBarrierAuto barriers[] = {
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				colorTexture.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				albedoTexture.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				normalTexture.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(barriers), barriers, 0, nullptr);
	}

	ShaderParameterTable SPT{};
	SPT.pushConstant("pushConstants", packedWidthHeight);
	SPT.texture("inSceneColor", passInput.sceneColorSRV);
	SPT.texture("inGBuffer0", passInput.gbuffer0SRV);
	SPT.texture("inGBuffer1", passInput.gbuffer1SRV);
	SPT.rwTexture("outColor", colorUAV.get());
	SPT.rwTexture("outAlbedo", albedoUAV.get());
	SPT.rwTexture("outNormal", normalUAV.get());

	uint32 requiredVolatiles = SPT.totalDescriptors();
	blitPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

	commandList->setComputePipelineState(blitPipelineState.get());

	DescriptorHeap* descriptorHeap = blitPassDescriptor.getDescriptorHeap(swapchainIndex);
	commandList->bindComputeShaderParameters(blitPipelineState.get(), &SPT, descriptorHeap);

	const uint32 dispatchX = (width + 7) / 8, dispatchY = (height + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);

	Texture::ReadbackRegion region = Texture::ReadbackRegion::mip0(colorTexture.get());
	colorReadbackHandle = colorTexture->requestReadback(commandList, region);
	albedoReadbackHandle = albedoTexture->requestReadback(commandList, region);
	normalReadbackHandle = normalTexture->requestReadback(commandList, region);
}

static void* tightenTextureData(void* pData, std::vector<uint8>& tightData, uint64 tightRowPitch, uint64 alignedRowPitch, uint32 height)
{
	if (tightRowPitch == alignedRowPitch) return pData;

	tightData.resize(tightRowPitch * height);

	uint8* pSrc = reinterpret_cast<uint8*>(pData);
	uint8* pDst = tightData.data();

	for (uint32 y = 0; y < height; ++y)
	{
		std::memcpy(pDst, pSrc, tightRowPitch);
		pSrc += alignedRowPitch;
		pDst += tightRowPitch;
	}

	return tightData.data();
}

static void* alignTextureData(void* pData, std::vector<uint8>& alignedData, uint64 tightRowPitch, uint64 alignedRowPitch, uint32 height)
{
	if (tightRowPitch == alignedRowPitch) return pData;

	alignedData.resize(alignedRowPitch * height);

	uint8* pSrc = reinterpret_cast<uint8*>(pData);
	uint8* pDst = alignedData.data();

	for (uint32 y = 0; y < height; ++y)
	{
		std::memcpy(pDst, pSrc, tightRowPitch);
		pSrc += tightRowPitch;
		pDst += alignedRowPitch;
	}

	return alignedData.data();
}

void DenoiserPluginPass::executeDenoiser(RenderCommandList* commandList, uint32 dstWidth, uint32 dstHeight, Texture* dst)
{
	CHECK(colorReadbackHandle->bAvailable);
	CHECK(albedoReadbackHandle->bAvailable);
	CHECK(normalReadbackHandle->bAvailable);

	DenoiserDevice* denoiserDevice = device->getDenoiserDevice();

	const uint32 width = denoisedTexture->getCreateParams().width;
	const uint32 height = denoisedTexture->getCreateParams().height;
	const uint64 dstRowPitch = denoisedTexture->getRowPitch();
	const uint64 dstSlicePitch = dstRowPitch * height;
	const uint64 tightRowPitch = 4 * sizeof(float) * width;

	std::vector<uint8> tightColorData, tightAlbedoData, tightNormalData;
	void* pColorData = tightenTextureData(colorReadbackHandle->readbackData, tightColorData, tightRowPitch, dstRowPitch, height);
	void* pAlbedoData = tightenTextureData(albedoReadbackHandle->readbackData, tightAlbedoData, tightRowPitch, dstRowPitch, height);
	void* pNormalData = tightenTextureData(normalReadbackHandle->readbackData, tightNormalData, tightRowPitch, dstRowPitch, height);

	std::vector<uint8> denoisedBuffer;
	denoiserDevice->denoise(pColorData, pAlbedoData, pNormalData, dstWidth, dstHeight, denoisedBuffer);

	// Reset for future readback request.
	colorReadbackHandle = nullptr;
	albedoReadbackHandle = nullptr;
	normalReadbackHandle = nullptr;

	std::vector<uint8> alignedDenoisedBuffer;
	void* pDenoisedData = alignTextureData(denoisedBuffer.data(), alignedDenoisedBuffer, tightRowPitch, dstRowPitch, height);
	denoisedTexture->uploadData(commandList, pDenoisedData, dstRowPitch, dstSlicePitch, 0);

	TextureBarrierAuto barrier{
		EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, EBarrierLayout::CopySource,
		denoisedTexture.get(), BarrierSubresourceRange::singleMip(0), ETextureBarrierFlags::None,
	};
	commandList->barrierAuto(0, nullptr, 1, &barrier, 0, nullptr);

	commandList->copyTexture2D(denoisedTexture.get(), dst);
}

void DenoiserPluginPass::resizeTextures(uint32 newWidth, uint32 newHeight)
{
	bool bShouldRecreate = colorTexture == nullptr
		|| colorTexture->getCreateParams().width != newWidth
		|| colorTexture->getCreateParams().height != newHeight;

	if (bShouldRecreate)
	{
		colorTexture.reset();
		albedoTexture.reset();
		normalTexture.reset();
		denoisedTexture.reset();

		const TextureCreateParams readbackTextureDesc = TextureCreateParams::texture2D(
			DENOISER_INPUT_FORMAT,
			ETextureAccessFlags::UAV | ETextureAccessFlags::CPU_READBACK,
			newWidth, newHeight, 1, 1, 0);

		colorTexture = UniquePtr<Texture>(device->createTexture(readbackTextureDesc));
		colorTexture->setDebugName(L"Texture_DenoiserInput_Color");

		albedoTexture = UniquePtr<Texture>(device->createTexture(readbackTextureDesc));
		albedoTexture->setDebugName(L"Texture_DenoiserInput_Albedo");

		normalTexture = UniquePtr<Texture>(device->createTexture(readbackTextureDesc));
		normalTexture->setDebugName(L"Texture_DenoiserInput_Normal");

		const TextureCreateParams uploadTextureDesc = TextureCreateParams::texture2D(
			DENOISER_INPUT_FORMAT,
			ETextureAccessFlags::UAV | ETextureAccessFlags::CPU_WRITE,
			newWidth, newHeight, 1, 1, 0);

		denoisedTexture = UniquePtr<Texture>(device->createTexture(uploadTextureDesc));
		denoisedTexture->setDebugName(L"Texture_DenoiserOutput");

		const UnorderedAccessViewDesc uavDesc{
			.format         = readbackTextureDesc.format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 }
		};

		colorUAV = UniquePtr<UnorderedAccessView>(device->createUAV(colorTexture.get(), uavDesc));
		albedoUAV = UniquePtr<UnorderedAccessView>(device->createUAV(albedoTexture.get(), uavDesc));
		normalUAV = UniquePtr<UnorderedAccessView>(device->createUAV(normalTexture.get(), uavDesc));
	}
}
