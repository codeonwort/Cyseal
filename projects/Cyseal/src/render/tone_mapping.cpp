#include "tone_mapping.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"

// Currently only for sceneColor SRV
#define MAX_VOLATILE_DESCRIPTORS 2

namespace RootParameters
{
	enum Value
	{
		InputTexturesSlot = 0,
		Count
	};
}

ToneMapping::ToneMapping()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	// Create root signature
	// - slot0: descriptor table (SRV)
	{
		DescriptorRange descriptorRange;
		// sceneColor       : register(t0)
		// indirectSpecular : register(t1)
		descriptorRange.init(EDescriptorRangeType::SRV, 2, 0);

		RootParameter rootParameters[RootParameters::Count];
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

	// Create volatile heaps for CBVs, SRVs, and UAVs for each frame
	volatileViewHeap.initialize(swapchainCount);
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::CBV_SRV_UAV;
		desc.numDescriptors = MAX_VOLATILE_DESCRIPTORS;
		desc.flags          = EDescriptorHeapFlags::ShaderVisible;
		desc.nodeMask       = 0;

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

		pipelineState = UniquePtr<PipelineState>(device->createGraphicsPipelineState(desc));
	}

	// Cleanup
	{
		delete shaderVS;
		delete shaderPS;
	}
}

void ToneMapping::renderToneMapping(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	Texture* sceneColor,
	Texture* indirectSpecular)
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
		gRenderDevice->copyDescriptors(
			1,
			heaps[0], VOLATILE_IX_SceneColor,
			sceneColor->getSourceSRVHeap(), sceneColor->getSRVDescriptorIndex());
		gRenderDevice->copyDescriptors(
			1,
			heaps[0], VOLATILE_IX_IndirectSpecular,
			indirectSpecular->getSourceSRVHeap(), indirectSpecular->getSRVDescriptorIndex());
		commandList->setGraphicsRootDescriptorTable(RootParameters::InputTexturesSlot, heaps[0], 0);
	}

	// Fullscreen triangle
	commandList->drawInstanced(3, 1, 0, 0);
}
