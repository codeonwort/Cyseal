#include "final_blit_pass.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"

void FinalBlitPass::initialize(RenderDevice* inDevice, uint32 inMaxBlitOperationsPerFrame)
{
	device = inDevice;
	maxBlitOperationsPerFrame = inMaxBlitOperationsPerFrame;

	const uint32 maxFramesInFlight = device->maxFramesInFlight();
	if (device->getCreateParams().swapChainParams.bHeadless == false)
	{
		rtvFormats.push_back(device->getSwapChain()->getBackbufferFormat());
		rtvIndexForSwapChain = 0;
	}
	rtvFormats.push_back(EPixelFormat::R32G32B32A32_FLOAT);
	rtvFormats.push_back(EPixelFormat::R16G16B16A16_FLOAT);
	rtvFormats.push_back(EPixelFormat::R8G8B8A8_UNORM);

	pipelineStates.initialize((uint32)rtvFormats.size());

	passDescriptor.initialize(device, L"FinalBlitPass", maxFramesInFlight, 0);

	// Create input layout
	inputLayout = {
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
	};

	// Load shader
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "FinalBlitVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "FinalBlitPS");
	shaderVS->declarePushConstants();
	shaderPS->declarePushConstants();
	shaderVS->loadFromFile(L"final_blit.hlsl", "mainVS");
	shaderPS->loadFromFile(L"final_blit.hlsl", "mainPS");

	// Create PSO
	std::vector<StaticSamplerDesc> staticSamplers = {
		StaticSamplerDesc{
			.name             = "sourceTextureSampler",
			.filter           = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT,
			.addressU         = ETextureAddressMode::Clamp,
			.addressV         = ETextureAddressMode::Clamp,
			.addressW         = ETextureAddressMode::Clamp,
			.mipLODBias       = 0.0f,
			.maxAnisotropy    = 0,
			.comparisonFunc   = EComparisonFunc::Always,
			.borderColor      = EStaticBorderColor::OpaqueBlack,
			.minLOD           = 0.0f,
			.maxLOD           = FLT_MAX,
			.shaderVisibility = EShaderVisibility::All,
		},
	};
	for (size_t i = 0; i < rtvFormats.size(); ++i)
	{
		GraphicsPipelineDesc pipelineDesc{
			.vs                     = shaderVS,
			.ps                     = shaderPS,
			.blendDesc              = BlendDesc::noBlend(),
			.sampleMask             = 0xffffffff,
			.rasterizerDesc         = RasterizerDesc::FrontCull(),
			.depthstencilDesc       = DepthstencilDesc::NoDepth(),
			.inputLayout            = inputLayout,
			.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
			.numRenderTargets       = 1,
			.rtvFormats             = { rtvFormats[i] },
			.dsvFormat              = EPixelFormat::UNKNOWN, // No depth so don't care
			.sampleDesc             = SampleDesc{ .count = 1u, .quality = 0, },
			.staticSamplers         = staticSamplers,
		};
		pipelineStates[i] = UniquePtr<GraphicsPipelineState>(device->createGraphicsPipelineState(pipelineDesc));
	}

	// Cleanup.
	{
		delete shaderVS;
		delete shaderPS;
	}
}

void FinalBlitPass::resetPerFrameResources(const FrameInfo& frameInfo)
{
	descriptorIndexTracker.reset();
	currentBlitOperations = 0;

	// #todo-rhi: Now it's distant from actual SPT setup, more error prone.
	uint32 requiredVolatiles = 0;
	requiredVolatiles += 1; // sceneUniform
	requiredVolatiles += 1; // sourceTexture
	passDescriptor.resizeDescriptorHeap(frameInfo, requiredVolatiles * maxBlitOperationsPerFrame);
}

void FinalBlitPass::renderFinalBlit(RenderCommandList* commandList, const FrameInfo& frameInfo, const FinalBlitPassInput& passInput)
{
	CHECK(currentBlitOperations < maxBlitOperationsPerFrame);
	currentBlitOperations += 1;

	std::vector<TextureBarrierAuto> textureBarriers = {
		TextureBarrierAuto::toShaderResource(passInput.sourceTexture, EBarrierSync::PIXEL_SHADING)
	};
	// Can be null for present
	if (passInput.renderTarget != nullptr)
	{
		textureBarriers.emplace_back(TextureBarrierAuto::toRenderTarget(passInput.renderTarget));
	}
	commandList->barrierAuto(0, nullptr, (uint32)textureBarriers.size(), textureBarriers.data(), 0, nullptr);

	// When modified, check resetPerFrameResources() if SPT size is correct.
	ShaderParameterTable SPT{};
	SPT.constantBuffer("sceneUniform", passInput.sceneUniformCBV);
	SPT.texture("sourceTexture", passInput.sourceSRV);

	// Assumes set by caller.
	//commandList->rsSetViewport(passInput.viewport);
	//commandList->rsSetScissorRect(passInput.scissorRect);

	GraphicsPipelineState* pipelineState = getPipelineState(passInput.renderTarget);
	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(frameInfo);

	commandList->omSetRenderTarget(passInput.renderTargetRTV, nullptr);

	commandList->setGraphicsPipelineState(pipelineState);
	commandList->bindGraphicsShaderParameters(pipelineState, &SPT, volatileHeap, &descriptorIndexTracker);
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	commandList->beginRenderPass();
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
	commandList->endRenderPass();
}

GraphicsPipelineState* FinalBlitPass::getPipelineState(Texture* renderTarget) const
{
	if (renderTarget == nullptr)
	{
		CHECK(rtvIndexForSwapChain >= 0);
		return pipelineStates.at(rtvIndexForSwapChain);
	}
	for (size_t i = 0; i < rtvFormats.size(); ++i)
	{
		if (rtvFormats[i] == renderTarget->getCreateParams().format)
		{
			return pipelineStates.at(i);
		}
	}
	CHECK_NO_ENTRY();
	return nullptr;
}
