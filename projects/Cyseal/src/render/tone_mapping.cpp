#include "tone_mapping.h"
#include "renderer_constants.h"

#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/shader.h"

void ToneMapping::initialize(RenderDevice* inDevice)
{
	RenderDevice* device = inDevice;
	const uint32 swapchainCount = device->maxFramesInFlight();

	rtvFormats.push_back(PF_finalSceneColor);
	pipelineStates.initialize((uint32)rtvFormats.size());

	passDescriptor.initialize(L"ToneMapping", swapchainCount, 0);

	// Create input layout
	inputLayout = {
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
	};

	// Load shader
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "ToneMappingVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "ToneMappingPS");
	shaderVS->declarePushConstants();
	shaderPS->declarePushConstants();
	shaderVS->loadFromFile(L"tone_mapping.hlsl", "mainVS");
	shaderPS->loadFromFile(L"tone_mapping.hlsl", "mainPS");

	// Create PSO
	std::vector<StaticSamplerDesc> staticSamplers = {
		StaticSamplerDesc{
			.name             = "sceneColorSampler",
			.filter           = ETextureFilter::MIN_MAG_MIP_POINT,
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
			.blendDesc              = BlendDesc(),
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

void ToneMapping::renderToneMapping(RenderCommandList* commandList, uint32 swapchainIndex, const ToneMappingInput& passInput)
{
	GraphicsPipelineState* pipelineState = getPipelineState(passInput.renderTarget);

	ShaderParameterTable SPT{};
	SPT.constantBuffer("sceneUniform", passInput.sceneUniformCBV);
	SPT.texture("sceneColor", passInput.sceneColorSRV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);

	commandList->rsSetViewport(passInput.viewport);
	commandList->rsSetScissorRect(passInput.scissorRect);

	commandList->setGraphicsPipelineState(pipelineState);
	commandList->bindGraphicsShaderParameters(pipelineState, &SPT, volatileHeap);
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	commandList->beginRenderPass();
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
	commandList->endRenderPass();
}

GraphicsPipelineState* ToneMapping::getPipelineState(Texture* renderTarget) const
{
	CHECK(renderTarget != nullptr);
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
