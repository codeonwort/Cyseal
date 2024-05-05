#include "buffer_visualization.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
#include "rhi/shader.h"
#include "rhi/texture_manager.h"

namespace RootParameters
{
	enum Value
	{
		ModeEnumSlot = 0,
		InputTexturesSlot,
		Count
	};
};

void BufferVisualization::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	// Create root signature.
	{
		DescriptorRange descriptorRange;
		// sceneColor       : register(t0, space0)
		// indirectSpecular : register(t1, space0)
		descriptorRange.init(EDescriptorRangeType::SRV, 2, 0, 0);

		RootParameter rootParameters[RootParameters::Count];
		rootParameters[RootParameters::ModeEnumSlot].initAsConstants(0, 0, 1); // register(b0, space0)
		rootParameters[RootParameters::InputTexturesSlot].initAsDescriptorTable(1, &descriptorRange);

		constexpr uint32 NUM_STATIC_SAMPLERS = 1;
		StaticSamplerDesc staticSamplers[NUM_STATIC_SAMPLERS];

		memset(staticSamplers + 0, 0, sizeof(staticSamplers[0]));
		staticSamplers[0].filter = ETextureFilter::MIN_MAG_MIP_POINT;
		staticSamplers[0].addressU = ETextureAddressMode::Clamp;
		staticSamplers[0].addressV = ETextureAddressMode::Clamp;
		staticSamplers[0].addressW = ETextureAddressMode::Clamp;
		staticSamplers[0].shaderVisibility = EShaderVisibility::Pixel;

		RootSignatureDesc rootSigDesc(
			RootParameters::Count,
			rootParameters,
			NUM_STATIC_SAMPLERS,
			staticSamplers,
			ERootSignatureFlags::AllowInputAssemblerInputLayout);

		rootSignature = UniquePtr<RootSignature>(device->createRootSignature(rootSigDesc));
	}

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
	shaderVS->loadFromFile(L"buffer_visualization.hlsl", "mainVS");
	shaderPS->loadFromFile(L"buffer_visualization.hlsl", "mainPS");

	// Create PSO.
	{
		GraphicsPipelineDesc desc{
			.rootSignature          = rootSignature.get(),
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

		pipelineState = UniquePtr<PipelineState>(device->createGraphicsPipelineState(desc));
	}

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
	commandList->setPipelineState(pipelineState.get());
	commandList->setGraphicsRootSignature(rootSignature.get());

	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	// Resource binding
	{
		DescriptorHeap* heaps[] = { volatileViewHeap.at(swapchainIndex) };
		commandList->setDescriptorHeaps(1, heaps);

		constexpr uint32 VOLATILE_IX_SceneColor = 0;
		constexpr uint32 VOLATILE_IX_IndirectSpecular = 1;

		auto copyDescriptor = [&](uint32 volatileIx, ShaderResourceView* srv)
		{
			gRenderDevice->copyDescriptors(
				1,
				heaps[0], volatileIx,
				srv->getSourceHeap(), srv->getDescriptorIndexInHeap());
		};

		copyDescriptor(VOLATILE_IX_SceneColor, sources.sceneColorSRV);
		copyDescriptor(VOLATILE_IX_IndirectSpecular, sources.indirectSpecularSRV);

		commandList->setGraphicsRootConstant32(RootParameters::ModeEnumSlot, (uint32)sources.mode, 0);
		commandList->setGraphicsRootDescriptorTable(RootParameters::InputTexturesSlot, heaps[0], 0);
	}

	// Fullscreen triangle
	commandList->drawInstanced(3, 1, 0, 0);
}
