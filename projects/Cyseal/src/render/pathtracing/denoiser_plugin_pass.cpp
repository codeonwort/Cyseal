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
}

void DenoiserPluginPass::executeDenoiser()
{
	DenoiserDevice* denoiserDevice = gRenderDevice->getDenoiserDevice();

	denoiserDevice->denoise(colorTexture.get(), albedoTexture.get(), normalTexture.get(), denoisedTexture.get());
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

		TextureCreateParams textureDesc = TextureCreateParams::texture2D(
			DENOISER_INPUT_FORMAT,
			ETextureAccessFlags::UAV | ETextureAccessFlags::CPU_WRITE,
			newWidth, newHeight, 1, 1, 0);

		colorTexture = UniquePtr<Texture>(gRenderDevice->createTexture(textureDesc));
		colorTexture->setDebugName(L"Texture_DenoiserInput_Color");

		albedoTexture = UniquePtr<Texture>(gRenderDevice->createTexture(textureDesc));
		albedoTexture->setDebugName(L"Texture_DenoiserInput_Albedo");

		normalTexture = UniquePtr<Texture>(gRenderDevice->createTexture(textureDesc));
		normalTexture->setDebugName(L"Texture_DenoiserInput_Normal");

		denoisedTexture = UniquePtr<Texture>(gRenderDevice->createTexture(textureDesc));
		denoisedTexture->setDebugName(L"Texture_DenoiserOutput");

		UnorderedAccessViewDesc uavDesc{
			.format         = textureDesc.format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 }
		};

		colorUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(colorTexture.get(), uavDesc));
		albedoUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(albedoTexture.get(), uavDesc));
		normalUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(normalTexture.get(), uavDesc));
	}
}
