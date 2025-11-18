#include "tone_mapping.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"

void ToneMapping::initialize(RenderDevice* device)
{
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	passDescriptor.initialize(L"ToneMapping", swapchainCount, 0);

	// Create input layout
	{
		inputLayout = {
			{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
		};
	}

	// Load shader
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "ToneMappingVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "ToneMappingPS");
	shaderVS->declarePushConstants();
	shaderPS->declarePushConstants();
	shaderVS->loadFromFile(L"tone_mapping.hlsl", "mainVS");
	shaderPS->loadFromFile(L"tone_mapping.hlsl", "mainPS");

	// Create PSO
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
		.rtvFormats             = { swapchain->getBackbufferFormat(), },
		.dsvFormat              = swapchain->getBackbufferDepthFormat(),
		.sampleDesc = SampleDesc{
			.count              = swapchain->supports4xMSAA() ? 4u : 1u,
			.quality            = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0,
		},
	};
	pipelineState = UniquePtr<GraphicsPipelineState>(device->createGraphicsPipelineState(pipelineDesc));

	// Cleanup
	{
		delete shaderVS;
		delete shaderPS;
	}
}

void ToneMapping::renderToneMapping(RenderCommandList* commandList, uint32 swapchainIndex, const ToneMappingInput& passInput)
{
	ShaderParameterTable SPT{};
	SPT.constantBuffer("sceneUniform", passInput.sceneUniformCBV);
	SPT.texture("sceneColor", passInput.sceneColorSRV);
	SPT.texture("sceneDepth", passInput.sceneDepthSRV);
	SPT.texture("gbuffer0", passInput.gbuffer0SRV);
	SPT.texture("gbuffer1", passInput.gbuffer1SRV);
	SPT.texture("indirectDiffuse", passInput.indirectDiffuseSRV);
	SPT.texture("indirectSpecular", passInput.indirectSpecularSRV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);

	commandList->rsSetViewport(passInput.viewport);
	commandList->rsSetScissorRect(passInput.scissorRect);

	commandList->setGraphicsPipelineState(pipelineState.get());
	commandList->bindGraphicsShaderParameters(pipelineState.get(), &SPT, volatileHeap);
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	commandList->beginRenderPass();
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
	commandList->endRenderPass();
}
