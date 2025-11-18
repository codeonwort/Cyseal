#include "buffer_visualization.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
#include "rhi/shader.h"
#include "rhi/texture_manager.h"

void BufferVisualization::initialize(RenderDevice* inRenderDevice)
{
	RenderDevice* device = inRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	passDescriptor.initialize(L"BufferVisualization", swapchainCount, 0);

	// Create input layout.
	VertexInputLayout inputLayout = {
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
	};

	// Load shaders.
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "BufferVisualizationVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "BufferVisualizationPS");
	shaderVS->declarePushConstants();
	shaderPS->declarePushConstants({ { "pushConstants", 1} });
	shaderVS->loadFromFile(L"buffer_visualization.hlsl", "mainVS");
	shaderPS->loadFromFile(L"buffer_visualization.hlsl", "mainPS");

	// Create PSO.
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
		.rtvFormats             = { swapchain->getBackbufferFormat() },
		.dsvFormat              = swapchain->getBackbufferDepthFormat(),
		.sampleDesc             = SampleDesc { .count = 1, .quality = 0 },
	};
	pipelineState = UniquePtr<GraphicsPipelineState>(device->createGraphicsPipelineState(pipelineDesc));

	// Cleanup.
	{
		delete shaderVS;
		delete shaderPS;
	}
}

void BufferVisualization::renderVisualization(RenderCommandList* commandList, uint32 swapchainIndex, const BufferVisualizationInput& passInput)
{
	ShaderParameterTable SPT{};
	SPT.pushConstant("pushConstants", (uint32)passInput.mode);
	SPT.texture("gbuffer0", passInput.gbuffer0SRV);
	SPT.texture("gbuffer1", passInput.gbuffer1SRV);
	SPT.texture("sceneColor", passInput.sceneColorSRV);
	SPT.texture("shadowMask", passInput.shadowMaskSRV);
	SPT.texture("indirectDiffuse", passInput.indirectDiffuseSRV);
	SPT.texture("indirectSpecular", passInput.indirectSpecularSRV);
	SPT.texture("velocityMap", passInput.velocityMapSRV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);

	commandList->setGraphicsPipelineState(pipelineState.get());
	commandList->bindGraphicsShaderParameters(pipelineState.get(), &SPT, volatileHeap);
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	commandList->beginRenderPass();
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
	commandList->endRenderPass();
}
