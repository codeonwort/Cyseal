#include "base_pass.h"
#include "render_device.h"
#include "swap_chain.h"
#include "resource_binding.h"
#include "shader.h"
#include "render_command.h"
#include "texture_manager.h"
#include "material.h"
#include "static_mesh.h"

#define MAX_VOLATILE_DESCRIPTORS 1024

void BasePass::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 bufferCount = swapchain->getBufferCount();

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

		memset(staticSamplers + 0, 0, sizeof(staticSamplers[0]));
		staticSamplers[0].filter = ETextureFilter::MIN_MAG_MIP_POINT;
		staticSamplers[0].addressU = ETextureAddressMode::Wrap;
		staticSamplers[0].addressV = ETextureAddressMode::Wrap;
		staticSamplers[0].addressW = ETextureAddressMode::Wrap;
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
		desc.flags          = EDescriptorHeapFlags::None;
		desc.nodeMask       = 0;

		cbvHeap[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));
		constantBuffers[i] = std::unique_ptr<ConstantBuffer>(
			device->createConstantBuffer(cbvHeap[i].get(), PAYLOAD_HEAP_SIZE, PAYLOAD_SIZE_ALIGNED));
	}

	// Create volatile heaps for CBVs, SRVs, and UAVs for each frame
	volatileViewHeaps.resize(bufferCount);
	for (uint32 i = 0; i < bufferCount; ++i)
	{
		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::CBV_SRV_UAV;
		desc.numDescriptors = MAX_VOLATILE_DESCRIPTORS;
		desc.flags          = EDescriptorHeapFlags::ShaderVisible;
		desc.nodeMask       = 0;

		volatileViewHeaps[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));

		wchar_t debugName[256];
		swprintf_s(debugName, L"BasePass_VolatileViewHeap_%u", i);
		volatileViewHeaps[i]->setDebugName(debugName);
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

void BasePass::renderBasePass(
	RenderCommandList* commandList,
	const SceneProxy* scene,
	const Camera* camera)
{
	// Draw static meshes
	const Matrix viewProjection = camera->getMatrix();
	// #todo: Support Other topologies
	const EPrimitiveTopology primitiveTopology = EPrimitiveTopology::TRIANGLELIST;

	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature
	//   Setting a PSO does not change the root signature.
	//   The application must call a dedicated API for setting the root signature.
	commandList->setPipelineState(pipelineState.get());
	commandList->setGraphicsRootSignature(rootSignature.get());
	
	commandList->iaSetPrimitiveTopology(primitiveTopology);

	// #todo: There might be duplicate descriptors between meshes. Needs a drawcall sorting mechanism.
	uint32 numVolatileDescriptors = 0;
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		numVolatileDescriptors += (uint32)mesh->getSections().size();
	}
	bindRootParameters(commandList, numVolatileDescriptors);

	uint32 payloadID = 0;
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		// #todo-wip: constant buffer
		const Matrix model = mesh->getTransform().getMatrix();
		const Matrix MVP = model * viewProjection;

		BasePass::ConstantBufferPayload payload;
		payload.mvpTransform = MVP;
		payload.r = 0.0f;
		payload.g = 1.0f;
		payload.b = 0.0f;
		payload.a = 1.0f;

		updateConstantBuffer(payloadID, &payload, sizeof(payload));

		// rootParameterIndex, constant, destOffsetIn32BitValues
		commandList->setGraphicsRootConstant32(0, payloadID, 0);

		for (const StaticMeshSection& section : mesh->getSections())
		{
			updateMaterial(commandList, payloadID, section.material);

			commandList->iaSetVertexBuffers(0, 1, &section.positionBuffer);
			commandList->iaSetIndexBuffer(section.indexBuffer);
			commandList->drawIndexedInstanced(section.indexBuffer->getIndexCount(), 1, 0, 0, 0);
		}

		++payloadID;
	}
}

void BasePass::bindRootParameters(RenderCommandList* cmdList, uint32 inNumPayloads)
{
	numPayloads = inNumPayloads;
	CHECK(numPayloads <= MAX_VOLATILE_DESCRIPTORS);

	uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	// slot0: Updated per drawcall, not here
	//cmdList->setGraphicsRootConstant32(0, payloadID, 0);

	// #todo-sampler: volatile sampler heap in the second element
	DescriptorHeap* heaps[] = { volatileViewHeaps[frameIndex].get() };
	cmdList->setDescriptorHeaps(1, heaps);

	// slot1: This was meant to be set per drawcall, but I'm accidentally
	// using an AoS for the register(c1), so let's keep it here for now.
	// Maybe this can be even better performant who knows?
	gRenderDevice->copyDescriptors(
		numPayloads,
		heaps[0], 0,
		cbvHeap[frameIndex].get(), 0);
	cmdList->setGraphicsRootDescriptorTable(1, heaps[0], 0);

	// slot2: Updated per drawcall, not here
	//cmdList->setGraphicsRootDescriptorTable(2, heaps[0] + someOffsetForCurrentMesh);
}

void BasePass::updateConstantBuffer(uint32 payloadID, void* payload, uint32 payloadSize)
{
	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	constantBuffers[frameIndex]->upload(payloadID, payload, payloadSize);
}

void BasePass::updateMaterial(RenderCommandList* cmdList, uint32 payloadID, Material* material)
{
	Texture* albedo = gTextureManager->getSystemTextureGrey2D();
	if (material) {
		albedo = material->albedo;
	}

	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	const uint32 numSRVs = 1; // For this drawcall
	DescriptorHeap* volatileHeap = volatileViewHeaps[frameIndex].get();

	uint32 descriptorStartOffset = numPayloads; // SRVs come right after CBVs
	descriptorStartOffset += payloadID * numSRVs;
	
	gRenderDevice->copyDescriptors(
		numSRVs,
		volatileHeap, descriptorStartOffset,
		gTextureManager->getSRVHeap(), albedo->getSRVDescriptorIndex());
	cmdList->setGraphicsRootDescriptorTable(2, volatileHeap, descriptorStartOffset);
}
