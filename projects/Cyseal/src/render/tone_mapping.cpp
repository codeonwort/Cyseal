#include "tone_mapping.h"
#include "render_device.h"
#include "swap_chain.h"
#include "gpu_resource_binding.h"
#include "shader.h"
#include "render_command.h"
#include "texture_manager.h"
#include "texture.h"

// Currently only for sceneColor SRV
#define MAX_VOLATILE_DESCRIPTORS 2

ToneMapping::ToneMapping()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	// Create root signature
	// - slot0: descriptor table (SRV)
	{
		constexpr uint32 NUM_ROOT_PARAMETERS = 1;
		RootParameter slotRootParameters[NUM_ROOT_PARAMETERS];

		DescriptorRange descriptorRange;
		// sceneColor       : register(t0)
		// indirectSpecular : register(t1)
		descriptorRange.init(EDescriptorRangeType::SRV, 2, 0);

		slotRootParameters[0].initAsDescriptorTable(1, &descriptorRange);

		constexpr uint32 NUM_STATIC_SAMPLERS = 1;
		StaticSamplerDesc staticSamplers[NUM_STATIC_SAMPLERS];

		memset(staticSamplers + 0, 0, sizeof(staticSamplers[0]));
		staticSamplers[0].filter = ETextureFilter::MIN_MAG_MIP_POINT;
		staticSamplers[0].addressU = ETextureAddressMode::Clamp;
		staticSamplers[0].addressV = ETextureAddressMode::Clamp;
		staticSamplers[0].addressW = ETextureAddressMode::Clamp;
		staticSamplers[0].shaderVisibility = EShaderVisibility::Pixel;

		RootSignatureDesc rootSigDesc(
			NUM_ROOT_PARAMETERS,
			slotRootParameters,
			NUM_STATIC_SAMPLERS,
			staticSamplers,
			ERootSignatureFlags::AllowInputAssemblerInputLayout);

		rootSignature = std::unique_ptr<RootSignature>(device->createRootSignature(rootSigDesc));
	}

	// Create volatile heaps for CBVs, SRVs, and UAVs for each frame
	volatileViewHeaps.resize(swapchainCount);
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::CBV_SRV_UAV;
		desc.numDescriptors = MAX_VOLATILE_DESCRIPTORS;
		desc.flags          = EDescriptorHeapFlags::ShaderVisible;
		desc.nodeMask       = 0;

		volatileViewHeaps[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));

		wchar_t debugName[256];
		swprintf_s(debugName, L"ToneMapping_VolatileViewHeap_%u", i);
		volatileViewHeaps[i]->setDebugName(debugName);
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
	shaderVS->loadFromFile(L"tone_mapping.hlsl", "mainVS");
	shaderPS->loadFromFile(L"tone_mapping.hlsl", "mainPS");

	// Create PSO
	{
		GraphicsPipelineDesc desc;
		desc.inputLayout            = inputLayout;
		desc.rootSignature          = rootSignature.get();
		desc.vs                     = shaderVS;
		desc.ps                     = shaderPS;
		desc.rasterizerDesc         = RasterizerDesc::FrontCull();
		desc.blendDesc              = BlendDesc();
		desc.depthstencilDesc       = DepthstencilDesc::NoDepth();
		desc.sampleMask             = 0xffffffff;
		desc.primitiveTopologyType  = EPrimitiveTopologyType::Triangle;
		desc.numRenderTargets       = 1;
		desc.rtvFormats[0]          = swapchain->getBackbufferFormat();
		desc.sampleDesc.count       = swapchain->supports4xMSAA() ? 4 : 1;
		desc.sampleDesc.quality     = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0;
		desc.dsvFormat              = swapchain->getBackbufferDepthFormat();

		pipelineState = std::unique_ptr<PipelineState>(device->createGraphicsPipelineState(desc));
	}

	// Cleanup
	{
		delete shaderVS;
		delete shaderPS;
	}
}

void ToneMapping::renderToneMapping(
	RenderCommandList* commandList,
	Texture* sceneColor,
	Texture* indirectSpecular)
{
	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	commandList->setPipelineState(pipelineState.get());
	commandList->setGraphicsRootSignature(rootSignature.get());

	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	// Resource binding
	{
		DescriptorHeap* heaps[] = { volatileViewHeaps[frameIndex].get() };
		commandList->setDescriptorHeaps(1, heaps);

		gRenderDevice->copyDescriptors(
			1,
			heaps[0], 0,
			sceneColor->getSourceSRVHeap(), sceneColor->getSRVDescriptorIndex());
		gRenderDevice->copyDescriptors(
			1,
			heaps[0], 1,
			indirectSpecular->getSourceSRVHeap(), indirectSpecular->getSRVDescriptorIndex());
		commandList->setGraphicsRootDescriptorTable(0, heaps[0], 0);
	}

	// Fullscreen triangle
	commandList->drawInstanced(3, 1, 0, 0);
}
