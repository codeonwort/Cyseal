#include "buffer_visualization.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
#include "rhi/shader.h"
#include "rhi/texture_manager.h"

void BufferVisualization::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	uint32 requiredVolatileDescriptors = 0;
	requiredVolatileDescriptors += 1; // sceneColor
	requiredVolatileDescriptors += 1; // indirectSpecular

	// Create volatile heap.
	volatileViewHeap.initialize(swapchainCount);
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		DescriptorHeapDesc desc{
			.type           = EDescriptorHeapType::CBV_SRV_UAV,
			.numDescriptors = requiredVolatileDescriptors,
			.flags          = EDescriptorHeapFlags::ShaderVisible,
			.nodeMask       = 0,
		};

		volatileViewHeap[i] = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(desc));

		wchar_t debugName[256];
		swprintf_s(debugName, L"BufferVisualization_VolatileViewHeap_%u", i);
		volatileViewHeap[i]->setDebugName(debugName);
	}

	// Create input layout.
	VertexInputLayout inputLayout = {
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
	};

	// Load shaders.
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "BufferVisualizationVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "BufferVisualizationPS");
	shaderVS->declarePushConstants();
	shaderPS->declarePushConstants({ "pushConstants" });
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

void BufferVisualization::renderVisualization(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const BufferVisualizationSources& sources)
{
	ShaderParameterTable SPT{};
	SPT.pushConstant("pushConstants", (uint32)sources.mode);
	SPT.texture("sceneColor", sources.sceneColorSRV);
	SPT.texture("indirectSpecular", sources.indirectSpecularSRV);

	commandList->setGraphicsPipelineState(pipelineState.get());
	commandList->bindGraphicsShaderParameters(pipelineState.get(), &SPT, volatileViewHeap.at(swapchainIndex));
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
}
