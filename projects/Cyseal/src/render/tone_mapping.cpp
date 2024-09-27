#include "tone_mapping.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"

// Currently only for sceneColor SRV
#define MAX_VOLATILE_DESCRIPTORS 2

void ToneMapping::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	// Create volatile heaps for CBVs, SRVs, and UAVs for each frame
	volatileViewHeap.initialize(swapchainCount);
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		DescriptorHeapDesc desc{
			.type           = EDescriptorHeapType::CBV_SRV_UAV,
			.numDescriptors = MAX_VOLATILE_DESCRIPTORS,
			.flags          = EDescriptorHeapFlags::ShaderVisible,
			.nodeMask       = 0,
		};
		volatileViewHeap[i] = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(desc));

		wchar_t debugName[256];
		swprintf_s(debugName, L"ToneMapping_VolatileViewHeap_%u", i);
		volatileViewHeap[i]->setDebugName(debugName);
	}

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

void ToneMapping::renderToneMapping(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	ShaderResourceView* sceneColorSRV,
	ShaderResourceView* indirectSpecularSRV)
{
	ShaderParameterTable SPT{};
	SPT.texture("sceneColor", sceneColorSRV);
	SPT.texture("indirectSpecular", indirectSpecularSRV);

	commandList->setGraphicsPipelineState(pipelineState.get());
	commandList->bindGraphicsShaderParameters(pipelineState.get(), &SPT, volatileViewHeap.at(swapchainIndex));
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
}
