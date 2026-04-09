#include "buffer_visualization.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/render_command.h"
#include "rhi/shader.h"
#include "rhi/texture_manager.h"

void BufferVisualization::initialize(RenderDevice* inRenderDevice)
{
	RenderDevice* device = inRenderDevice;

	const uint32 swapchainCount = device->maxFramesInFlight();
	if (device->getCreateParams().swapChainParams.bHeadless == false)
	{
		rtvFormats.push_back(device->getSwapChain()->getBackbufferFormat());
		rtvIndexForSwapChain = 0;
	}
	rtvFormats.push_back(EPixelFormat::R32G32B32A32_FLOAT);
	rtvFormats.push_back(EPixelFormat::R16G16B16A16_FLOAT);
	rtvFormats.push_back(EPixelFormat::R8G8B8A8_UNORM);

	pipelineStates.initialize((uint32)rtvFormats.size());

	passDescriptor.initialize(L"BufferVisualization", swapchainCount, 0);

	// Create input layout.
	VertexInputLayout inputLayout = {
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
	};

	// Load shaders.
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "BufferVisualizationVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "BufferVisualizationPS");
	shaderVS->declarePushConstants();
	shaderPS->declarePushConstants({ { "pushConstants", 3} });
	shaderVS->loadFromFile(L"buffer_visualization.hlsl", "mainVS");
	shaderPS->loadFromFile(L"buffer_visualization.hlsl", "mainPS");

	// Create PSO.
	std::vector<StaticSamplerDesc> staticSamplers = {
		StaticSamplerDesc{
			.name             = "textureSampler",
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
			.ds                     = nullptr,
			.hs                     = nullptr,
			.gs                     = nullptr,
			.blendDesc              = BlendDesc(),
			.sampleMask             = 0xffffffff,
			.rasterizerDesc         = RasterizerDesc::FrontCull(),
			.depthstencilDesc       = DepthstencilDesc::NoDepth(),
			.inputLayout            = inputLayout,
			.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
			.numRenderTargets       = 1,
			.rtvFormats             = { rtvFormats[i] },
			.dsvFormat              = EPixelFormat::UNKNOWN, // No depth so don't care
			.sampleDesc             = SampleDesc { .count = 1, .quality = 0 },
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

void BufferVisualization::renderVisualization(RenderCommandList* commandList, uint32 swapchainIndex, const BufferVisualizationInput& passInput)
{
	GraphicsPipelineState* pipelineState = getPipelineState(passInput.renderTarget);

	ShaderParameterTable SPT{};
	SPT.pushConstants("pushConstants", { (uint32)passInput.mode, passInput.textureWidth, passInput.textureHeight });
	SPT.texture("gbuffer0", passInput.gbuffer0SRV);
	SPT.texture("gbuffer1", passInput.gbuffer1SRV);
	SPT.texture("sceneColor", passInput.sceneColorSRV);
	SPT.texture("shadowMask", passInput.shadowMaskSRV);
	SPT.texture("indirectDiffuse", passInput.indirectDiffuseSRV);
	SPT.texture("indirectSpecular", passInput.indirectSpecularSRV);
	SPT.texture("velocityMap", passInput.velocityMapSRV);
	SPT.texture("visibilityBuffer", passInput.visibilityBufferSRV);
	SPT.texture("barycentricCoord", passInput.barycentricCoordSRV);
	SPT.texture("visGBuffer0", passInput.visGbuffer0SRV);
	SPT.texture("visGBuffer1", passInput.visGbuffer1SRV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);

	commandList->setGraphicsPipelineState(pipelineState);
	commandList->bindGraphicsShaderParameters(pipelineState, &SPT, volatileHeap);
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	commandList->beginRenderPass();
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
	commandList->endRenderPass();
}

GraphicsPipelineState* BufferVisualization::getPipelineState(Texture* renderTarget) const
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
