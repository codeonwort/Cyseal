#include "denoiser_plugin_pass.h"

#include "rhi/render_device.h"
#include "rhi/denoiser_device.h"
#include "rhi/swap_chain.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"

DEFINE_LOG_CATEGORY_STATIC(LogDenoiserPlugin);

// We will denoise only RGB but include A channel for UAV.
// Should provide proper stride so that the plugin only consider RGB channels.
static constexpr EPixelFormat DENOISER_INPUT_FORMAT = EPixelFormat::R32G32B32A32_FLOAT;

void DenoiserPluginPass::initialize()
{
	if (!isAvailable())
	{
		CYLOG(LogDenoiserPlugin, Warning, L"Denoiser device is unavailable. Denoiser pass will be disabled.");
		return;
	}

	// Shader
	{
		ShaderStage* blitShader = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "BlitDenoiserInputCS");
		blitShader->declarePushConstants({ "pushConstants" });
		blitShader->loadFromFile(L"blit_denoiser_input.hlsl", "mainCS");

		blitPipelineState = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
			ComputePipelineDesc{ .cs = blitShader, .nodeMask = 0 }
		));

		delete blitShader;
	}

	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();
	blitPassDescriptor.initialize(L"DenoiserPlugin_BlitPass", swapchainCount, 0);
}

bool DenoiserPluginPass::isAvailable() const
{
	return gRenderDevice->getDenoiserDevice()->isValid();
}

void DenoiserPluginPass::blitTextures(RenderCommandList* commandList, uint32 swapchainIndex, const DenoiserPluginInput& passInput)
{
	const uint32 width = passInput.imageWidth;
	const uint32 height = passInput.imageHeight;
	CHECK(width < 0xFFFF && height < 0xFFFF);
	const uint32 packedWidthHeight = ((width & 0xFFFF) << 16) | (height & 0xFFFF);

	resizeTextures(width, height);

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // pushConstants
		requiredVolatiles += 1; // inSceneColor
		requiredVolatiles += 1; // inGBuffer0
		requiredVolatiles += 1; // inGBuffer1
		requiredVolatiles += 1; // outColor
		requiredVolatiles += 1; // outAlbedo
		requiredVolatiles += 1; // outNormal

		blitPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	}
	DescriptorHeap* descriptorHeap = blitPassDescriptor.getDescriptorHeap(swapchainIndex);

	ShaderParameterTable SPT{};
	SPT.pushConstant("pushConstants", packedWidthHeight);
	SPT.texture("inSceneColor", passInput.sceneColorSRV);
	SPT.texture("inGBuffer0", passInput.gbuffer0SRV);
	SPT.texture("inGBuffer1", passInput.gbuffer1SRV);
	SPT.rwTexture("outColor", colorUAV.get());
	SPT.rwTexture("outAlbedo", albedoUAV.get());
	SPT.rwTexture("outNormal", normalUAV.get());

	const uint32 dispatchX = (width + 7) / 8, dispatchY = (height + 7) / 8;

	commandList->setComputePipelineState(blitPipelineState.get());
	commandList->bindComputeShaderParameters(blitPipelineState.get(), &SPT, descriptorHeap);
	commandList->dispatchCompute(dispatchX, dispatchY, 1);

	colorTexture->prepareReadback(commandList);
	albedoTexture->prepareReadback(commandList);
	normalTexture->prepareReadback(commandList);
}

void DenoiserPluginPass::executeDenoiser(RenderCommandList* commandList, Texture* dst)
{
	DenoiserDevice* denoiserDevice = gRenderDevice->getDenoiserDevice();

	std::vector<uint8> denoisedBuffer;
	denoiserDevice->denoise(colorTexture.get(), albedoTexture.get(), normalTexture.get(), denoisedBuffer);

	const uint32 width = denoisedTexture->getCreateParams().width;
	const uint32 height = denoisedTexture->getCreateParams().height;
	const uint64 rowPitch = denoisedTexture->getRowPitch();
	const uint64 slicePitch = rowPitch * height;

	denoisedTexture->uploadData(*commandList, denoisedBuffer.data(), rowPitch, slicePitch, 0);

	TextureMemoryBarrier barrier{
		ETextureMemoryLayout::COPY_DEST, ETextureMemoryLayout::COPY_SRC, denoisedTexture.get()
	};
	commandList->resourceBarriers(0, nullptr, 1, &barrier);

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

		colorTexture = UniquePtr<Texture>(gRenderDevice->createTexture(readbackTextureDesc));
		colorTexture->setDebugName(L"Texture_DenoiserInput_Color");

		albedoTexture = UniquePtr<Texture>(gRenderDevice->createTexture(readbackTextureDesc));
		albedoTexture->setDebugName(L"Texture_DenoiserInput_Albedo");

		normalTexture = UniquePtr<Texture>(gRenderDevice->createTexture(readbackTextureDesc));
		normalTexture->setDebugName(L"Texture_DenoiserInput_Normal");

		const TextureCreateParams uploadTextureDesc = TextureCreateParams::texture2D(
			DENOISER_INPUT_FORMAT,
			ETextureAccessFlags::UAV | ETextureAccessFlags::CPU_WRITE,
			newWidth, newHeight, 1, 1, 0);

		denoisedTexture = UniquePtr<Texture>(gRenderDevice->createTexture(uploadTextureDesc));
		denoisedTexture->setDebugName(L"Texture_DenoiserOutput");

		const UnorderedAccessViewDesc uavDesc{
			.format         = readbackTextureDesc.format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 }
		};

		colorUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(colorTexture.get(), uavDesc));
		albedoUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(albedoTexture.get(), uavDesc));
		normalUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(normalTexture.get(), uavDesc));
	}
}
