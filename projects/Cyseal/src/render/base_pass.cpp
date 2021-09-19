#include "base_pass.h"
#include "render_device.h"
#include "swap_chain.h"
#include "resource_binding.h"
#include "shader.h"
#include "render_command.h"

#include "static_mesh.h" // #todo-wip: for Material

void BasePass::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();

	// Create root signature
	{
		constexpr uint32 NUM_ROOT_PARAMETERS = 3;
		RootParameter slotRootParameters[NUM_ROOT_PARAMETERS];
		
		DescriptorRange descriptorRanges[2];
		descriptorRanges[0].init(EDescriptorRangeType::CBV, 1, 1);
		descriptorRanges[1].init(EDescriptorRangeType::SRV, 1, 0);

		slotRootParameters[0].initAsConstants(0, 0, 1);
		slotRootParameters[1].initAsDescriptorTable(1, &descriptorRanges[0]);
		slotRootParameters[2].initAsDescriptorTable(1, &descriptorRanges[1]);

		constexpr uint32 NUM_STATIC_SAMPLERS = 1;
		StaticSamplerDesc staticSamplers[NUM_STATIC_SAMPLERS];

		ZeroMemory(staticSamplers + 0, sizeof(staticSamplers[0]));
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

	// 1. Create cbv descriptor heaps
	// 2. Create constant buffers
	cbvHeap.resize(swapchain->getBufferCount());
	constantBuffers.resize(swapchain->getBufferCount());
	for (uint32 i = 0; i < swapchain->getBufferCount(); ++i)
	{
		constexpr uint32 PAYLOAD_HEAP_SIZE = 1024 * 64; // 64 KiB
		constexpr uint32 PAYLOAD_SIZE_ALIGNED = (sizeof(ConstantBufferPayload) + 255) & ~255;

		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::CBV_SRV_UAV;
		desc.numDescriptors = PAYLOAD_HEAP_SIZE / PAYLOAD_SIZE_ALIGNED;
		desc.flags = EDescriptorHeapFlags::ShaderVisible;
		desc.nodeMask = 0;

		cbvHeap[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));

		constantBuffers[i] = std::unique_ptr<ConstantBuffer>(
			device->createConstantBuffer(cbvHeap[i].get(), PAYLOAD_HEAP_SIZE, PAYLOAD_SIZE_ALIGNED));
	}

	// Create input layout
	{
		inputLayout = {
			{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
		};
	}

	// Load shader
	Shader* shader = nullptr;
	{
		shader = device->createShader();
		shader->loadVertexShader(L"base_pass.hlsl", "mainVS");
		shader->loadPixelShader(L"base_pass.hlsl", "mainPS");
	}

	// Create PSO
	{
		GraphicsPipelineDesc desc;
		desc.inputLayout            = inputLayout;
		desc.rootSignature          = rootSignature.get();
		desc.vs                     = shader->getVertexShader();
		desc.ps                     = shader->getPixelShader();
		desc.rasterizerDesc         = RasterizerDesc();
		desc.blendDesc              = BlendDesc();
		desc.depthstencilDesc       = DepthstencilDesc();
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
		//delete shader;
	}
}

void BasePass::bindRootParameter(RenderCommandList* cmdList)
{
	uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	// #todo-wip: Need to somehow bring srv heap from d3d_device to here.
	// https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
	//   Only one descriptor heap of each type can be set at one time,
	//   which means a maximum of 2 heaps (one sampler, one CBV/SRV/UAV) can be set at one time.
	DescriptorHeap* heaps[] = { cbvHeap[frameIndex].get() };
	cmdList->setDescriptorHeaps(1, heaps);
	cmdList->setGraphicsRootDescriptorTable(1, heaps[0]);
	//cmdList->setGraphicsRootDescriptorTable(2, heaps[0]);
}

void BasePass::updateConstantBuffer(uint32 payloadID, void* payload, uint32 payloadSize)
{
	uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	constantBuffers[frameIndex]->upload(payloadID, payload, payloadSize);
}

void BasePass::updateMaterial(uint32 payloadID, Material* material)
{
	// #todo-wip: updateMaterial
	if (material) {
		Texture* albedo = material->albedo;
	}
}
