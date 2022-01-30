#include "base_pass.h"
#include "render_device.h"
#include "swap_chain.h"
#include "resource_binding.h"
#include "shader.h"
#include "render_command.h"

#include "static_mesh.h" // todo-wip: Include static_mesh.h for Material

void BasePass::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 bufferCount = swapchain->getBufferCount();
	const uint32 descSizeCbvSrvUav = device->getDescriptorSizeCbvSrvUav();

	// Create root signature
	// - slot0: 32-bit constant (object id)
	// - slot1: descriptor table (CBV)
	// - slot2: descriptor table (SRV)
	{
		constexpr uint32 NUM_ROOT_PARAMETERS = 3;
		RootParameter slotRootParameters[NUM_ROOT_PARAMETERS];
		
		DescriptorRange descriptorRanges[2];
		descriptorRanges[0].init(EDescriptorRangeType::CBV, 1, 1 /*b1*/);
		descriptorRanges[1].init(EDescriptorRangeType::SRV, 1, 0 /*c0*/);

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
	cbvHeap.resize(bufferCount);
	constantBuffers.resize(bufferCount);
	for (uint32 i = 0; i < bufferCount; ++i)
	{
		constexpr uint32 PAYLOAD_HEAP_SIZE = 1024 * 64; // 64 KiB
		constexpr uint32 PAYLOAD_SIZE_ALIGNED = (sizeof(ConstantBufferPayload) + 255) & ~255;

		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::CBV_SRV_UAV;
		desc.numDescriptors = PAYLOAD_HEAP_SIZE / PAYLOAD_SIZE_ALIGNED;
		// todo-wip: Must be non shader-visible to copy descriptors from here to the volatile heap
		desc.flags          = EDescriptorHeapFlags::ShaderVisible;
		desc.nodeMask       = 0;

		cbvHeap[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));
		constantBuffers[i] = std::unique_ptr<ConstantBuffer>(
			device->createConstantBuffer(cbvHeap[i].get(), PAYLOAD_HEAP_SIZE, PAYLOAD_SIZE_ALIGNED));
	}

	// Create volatile heaps for CBVs, SRVs, and UAVs for each frame
	volatileViewHeaps.resize(bufferCount);
	for (uint32 i = 0; i < bufferCount; ++i)
	{
		constexpr uint32 VOLATILE_HEAP_SIZE = 1024 * 64; // 64 KiB

		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::CBV_SRV_UAV;
		desc.numDescriptors = VOLATILE_HEAP_SIZE / descSizeCbvSrvUav;
		desc.flags          = EDescriptorHeapFlags::ShaderVisible;
		desc.nodeMask       = 0;

		volatileViewHeaps[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));
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

void BasePass::bindRootParameters(RenderCommandList* cmdList, uint32 inNumPayloads)
{
	numPayloads = inNumPayloads;

	uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	// slot0: This is updated per drawcall.
	//cmdList->setGraphicsRootConstant32(0, payloadID, 0);

	// todo-wip: Need to somehow bring srv heap from d3d_device to here.
	// https://stackoverflow.com/questions/32114174/directx-12-how-to-use-commandlist-with-multiple-descriptor-heaps
	// The correty way is to allocate a large heap,
	// and copy descriptors from non-shader-visible heap to it on demand.
	
	// todo-wip: volatile sampler heap in the second element
	DescriptorHeap* heaps[] = { volatileViewHeaps[frameIndex].get() };
	cmdList->setDescriptorHeaps(1, heaps);

	// todo-wip: This was meant to be set per drawcall,
	// but I'm accidentally using an AoS for the register(c1), so let's keep it here for now.
	cmdList->setGraphicsRootDescriptorTable(1, heaps[0], 0);

	// todo-wip: SRVs are set per drawcall. Not here!
	//cmdList->setGraphicsRootDescriptorTable(2, heaps[0] + someOffsetForCurrentMesh);
}

void BasePass::updateConstantBuffer(uint32 payloadID, void* payload, uint32 payloadSize)
{
	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	constantBuffers[frameIndex]->upload(payloadID, payload, payloadSize);
}

void BasePass::updateMaterial(RenderCommandList* cmdList, uint32 payloadID, Material* material)
{
	// todo-wip: Fallback texture if invalid material
	Texture* albedo = nullptr;
	if (material) {
		albedo = material->albedo;
	}

	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	const uint32 numSRVs = 1; // For this drawcall

	// todo-wip: heapSRV is declared in d3d_device.h
	// 1. Copy SRV descriptors
	//gRenderDevice->copyDescriptors(numSRVs, 

	// 2. Set descriptor table
	uint32 descriptorStartOffset = numPayloads; // SRVs come right after CBVs
	descriptorStartOffset += payloadID * numSRVs;

	cmdList->setGraphicsRootDescriptorTable(2, volatileViewHeaps[0].get(), descriptorStartOffset);
}
