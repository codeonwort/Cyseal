#include "base_pass.h"
#include "render_device.h"
#include "swap_chain.h"
#include "resource_binding.h"
#include "shader.h"
#include "render_command.h"

void BasePass::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();

	// Create root signature
	{
		RootParameter slotRootParameters[1];
		
		DescriptorRange cbvTable;
		cbvTable.init(EDescriptorRangeType::CBV, 1, 0);

		slotRootParameters[0].initAsDescriptorTable(1, &cbvTable);

		RootSignatureDesc rootSigDesc(1, slotRootParameters, 0, nullptr, ERootSignatureFlags::AllowInputAssemblerInputLayout);

		rootSignature = std::unique_ptr<RootSignature>(device->createRootSignature(rootSigDesc));
	}
	// 1. Create cbv descriptor heaps
	// 2. Create constant buffers
	cbvHeap.resize(swapchain->getBufferCount());
	constantBuffers.resize(swapchain->getBufferCount());
	for (uint32 i = 0; i < swapchain->getBufferCount(); ++i)
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::CBV_SRV_UAV;
		desc.numDescriptors = 1;
		desc.flags = EDescriptorHeapFlags::ShaderVisible;
		desc.nodeMask = 0;

		cbvHeap[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));

		constantBuffers[i] = std::unique_ptr<ConstantBuffer>(
			device->createConstantBuffer(cbvHeap[i].get(), 1024 * 64, sizeof(ConstantBufferPayload)));
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

	DescriptorHeap* heaps[] = { cbvHeap[frameIndex].get() };
	cmdList->setDescriptorHeaps(1, heaps);
	cmdList->setGraphicsRootParameter(0, heaps[0]);
}

void BasePass::updateConstantBuffer(void* payload, uint32 payloadSize)
{
	uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	constantBuffers[frameIndex]->upload(payload, payloadSize);
}
