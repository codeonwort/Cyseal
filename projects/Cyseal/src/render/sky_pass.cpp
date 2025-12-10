#include "sky_pass.h"

#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"

void SkyPass::initialize(EPixelFormat sceneColorFormat)
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	volatileDescriptor.initialize(L"SkyPass", swapchainCount, 0);

	// Create input layout.
	VertexInputLayout inputLayout = {
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
	};

	// Load shaders.
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "SkyPassVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "SkyPassPS");
	shaderVS->declarePushConstants();
	shaderPS->declarePushConstants();
	shaderVS->loadFromFile(L"sky_pass.hlsl", "mainVS");
	shaderPS->loadFromFile(L"sky_pass.hlsl", "mainPS");

	// Create PSO.
	BlendDesc blendDesc{};
	blendDesc.renderTarget[0] = RenderTargetBlendDesc{
		.blendEnable           = true,
		.logicOpEnable         = false,
		.srcBlend              = EBlend::One,
		.destBlend             = EBlend::One,
		.blendOp               = EBlendOp::Add,
		.srcBlendAlpha         = EBlend::One,
		.destBlendAlpha        = EBlend::Zero,
		.blendOpAlpha          = EBlendOp::Add,
		.logicOp               = ELogicOp::Noop,
		.renderTargetWriteMask = EColorWriteEnable::All
	};
	DepthstencilDesc depthstencilDesc{
		.depthEnable      = true,
		.depthWriteMask   = EDepthWriteMask::Zero,
		.depthFunc        = EComparisonFunc::Equal,
		.stencilEnable    = false,
		.stencilReadMask  = 0xff,
		.stencilWriteMask = 0xff,
		.frontFace        = { EStencilOp::Keep, EStencilOp::Keep, EStencilOp::Keep, EComparisonFunc::Always },
		.backFace         = { EStencilOp::Keep, EStencilOp::Keep, EStencilOp::Keep, EComparisonFunc::Always }
	};
	std::vector<StaticSamplerDesc> staticSamplers = {
		StaticSamplerDesc{
			.name             = "skyboxSampler",
			.filter           = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT,
			.addressU         = ETextureAddressMode::Wrap,
			.addressV         = ETextureAddressMode::Wrap,
			.addressW         = ETextureAddressMode::Wrap,
			.mipLODBias       = 0.0f,
			.maxAnisotropy    = 0,
			.comparisonFunc   = EComparisonFunc::Always,
			.borderColor      = EStaticBorderColor::TransparentBlack,
			.minLOD           = 0.0f,
			.maxLOD           = 0.0f,
			.shaderVisibility = EShaderVisibility::All,
		},
	};
	GraphicsPipelineDesc pipelineDesc{
		.vs                     = shaderVS,
		.ps                     = shaderPS,
		.ds                     = nullptr,
		.hs                     = nullptr,
		.gs                     = nullptr,
		.blendDesc              = std::move(blendDesc),
		.sampleMask             = 0xffffffff,
		.rasterizerDesc         = RasterizerDesc::FrontCull(),
		.depthstencilDesc       = std::move(depthstencilDesc),
		.inputLayout            = inputLayout,
		.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
		.numRenderTargets       = 1,
		.rtvFormats             = { sceneColorFormat },
		.dsvFormat              = swapchain->getBackbufferDepthFormat(),
		.sampleDesc             = SampleDesc { .count = 1, .quality = 0 },
		.staticSamplers         = std::move(staticSamplers),
	};
	pipelineState = UniquePtr<GraphicsPipelineState>(device->createGraphicsPipelineState(pipelineDesc));

	// Cleanup.
	{
		delete shaderVS;
		delete shaderPS;
	}
}

void SkyPass::renderSky(RenderCommandList* commandList, uint32 swapchainIndex, const SkyPassInput& passInput)
{
	ShaderParameterTable SPT{};
	SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
	SPT.texture("skybox", passInput.skyboxSRV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	volatileDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

	auto descriptorHeap = volatileDescriptor.getDescriptorHeap(swapchainIndex);

	commandList->setGraphicsPipelineState(pipelineState.get());
	commandList->bindGraphicsShaderParameters(pipelineState.get(), &SPT, descriptorHeap);
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	commandList->beginRenderPass();
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
	commandList->endRenderPass();
}
