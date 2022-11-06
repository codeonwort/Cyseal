#include "base_pass.h"
#include "render_device.h"
#include "swap_chain.h"
#include "resource_binding.h"
#include "shader.h"
#include "render_command.h"
#include "texture_manager.h"
#include "material.h"
#include "static_mesh.h"
#include "vertex_buffer_pool.h"

#define MAX_VOLATILE_DESCRIPTORS 1024
// #todo: Acquire pixel format from Texture
#define PF_sceneColor            EPixelFormat::R32G32B32A32_FLOAT

struct SceneUniform
{
	vec3 sunDirection; float _pad0;
	vec3 sunIlluminance; float _pad1;
};

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
		constexpr uint32 NUM_ROOT_PARAMETERS = 4;
		RootParameter slotRootParameters[NUM_ROOT_PARAMETERS];
		
		DescriptorRange descriptorRanges[3];
		descriptorRanges[0].init(EDescriptorRangeType::CBV, 1, 1 /*b1*/);
		descriptorRanges[1].init(EDescriptorRangeType::CBV, 1, 2 /*b2 ~ b?*/);
		descriptorRanges[2].init(EDescriptorRangeType::SRV, 1, 0 /*c0*/);

		slotRootParameters[0].initAsConstants(0 /*b0*/, 0, 1); // object ID
		slotRootParameters[1].initAsDescriptorTable(1, &descriptorRanges[0]); // scene uniform
		// NOTE: Can't group material CBV and SRV into the same table.
		// numPayloads is varying, so setGraphicsRootDescriptorTable()
		// can't decidie where to start SRV descriptors if they are in the same table.
		slotRootParameters[2].initAsDescriptorTable(1, &descriptorRanges[1]); // material CBV
		slotRootParameters[3].initAsDescriptorTable(1, &descriptorRanges[2]); // material SRV

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
	materialConstantBuffers.resize(bufferCount);
	for (uint32 i = 0; i < bufferCount; ++i)
	{
		constexpr uint32 PAYLOAD_HEAP_SIZE = 1024 * 64; // 64 KiB
		constexpr uint32 PAYLOAD_SIZE_ALIGNED = (sizeof(ConstantBufferPayload) + 255) & ~255;

		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::CBV;
		desc.numDescriptors = PAYLOAD_HEAP_SIZE / PAYLOAD_SIZE_ALIGNED;
		desc.flags          = EDescriptorHeapFlags::None;
		desc.nodeMask       = 0;

		cbvHeap[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));
		materialConstantBuffers[i] = std::unique_ptr<ConstantBuffer>(
			device->createConstantBuffer(cbvHeap[i].get(), PAYLOAD_HEAP_SIZE, PAYLOAD_SIZE_ALIGNED));
	}

	sceneUniformHeaps.resize(bufferCount);
	sceneUniformBuffers.resize(bufferCount);
	for (uint32 i=0; i<bufferCount; ++i)
	{
		constexpr uint32 PAYLOAD_HEAP_SIZE = 1024 * 64; // 64 KiB
		constexpr uint32 PAYLOAD_SIZE_ALIGNED = (sizeof(SceneUniform) + 255) & ~255;

		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::CBV;
		desc.numDescriptors = PAYLOAD_HEAP_SIZE / PAYLOAD_SIZE_ALIGNED;
		desc.flags          = EDescriptorHeapFlags::None;
		desc.nodeMask       = 0;

		sceneUniformHeaps[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));
		sceneUniformBuffers[i] = std::unique_ptr<ConstantBuffer>(
			device->createConstantBuffer(sceneUniformHeaps[i].get(), PAYLOAD_HEAP_SIZE, PAYLOAD_SIZE_ALIGNED));
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
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "BasePassVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "BasePassPS");
	shaderVS->loadFromFile(L"base_pass.hlsl", "mainVS");
	shaderPS->loadFromFile(L"base_pass.hlsl", "mainPS");

	// Create PSO
	{
		GraphicsPipelineDesc desc;
		desc.inputLayout            = inputLayout;
		desc.rootSignature          = rootSignature.get();
		desc.vs                     = shaderVS;
		desc.ps                     = shaderPS;
		desc.rasterizerDesc         = RasterizerDesc();
		desc.blendDesc              = BlendDesc();
		desc.depthstencilDesc       = DepthstencilDesc::StandardSceneDepth();
		desc.sampleMask             = 0xffffffff;
		desc.primitiveTopologyType  = EPrimitiveTopologyType::Triangle;
		desc.numRenderTargets       = 1;
		desc.rtvFormats[0]          = PF_sceneColor;
		desc.sampleDesc.count       = swapchain->supports4xMSAA() ? 4 : 1;
		desc.sampleDesc.quality     = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0;
		desc.dsvFormat              = swapchain->getBackbufferDepthFormat();

		pipelineState = std::unique_ptr<PipelineState>(device->createGraphicsPipelineState(desc));
	}

	// Cleanup
	{
		// #todo-renderer
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

	// #todo-lod: LOD selection
	const uint32 LOD = 0;

	// #todo-renderer: There might be duplicate descriptors between meshes. Needs a drawcall sorting mechanism.
	uint32 numVolatileDescriptors = 0;
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		numVolatileDescriptors += (uint32)mesh->getSections(LOD).size();
	}
	bindRootParameters(commandList, numVolatileDescriptors);

	// Update scene uniform buffer
	{
		SceneUniform uboData;
		uboData.sunDirection = scene->sun.direction;
		uboData.sunIlluminance = scene->sun.illuminance;

		const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
		sceneUniformBuffers[frameIndex]->upload(0, &uboData, sizeof(uboData));
	}

	uint32 payloadID = 0;
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		for (const StaticMeshSection& section : mesh->getSections(LOD))
		{
			// #todo-wip: constant buffer
			const Matrix model = mesh->getTransform().getMatrix();
			const Matrix MVP = model * viewProjection;

			BasePass::ConstantBufferPayload payload;
			payload.mvpTransform = MVP;
			memcpy_s(payload.albedoMultiplier, sizeof(payload.albedoMultiplier),
				section.material->albedoMultiplier, sizeof(section.material->albedoMultiplier));

			// rootParameterIndex, constant, destOffsetIn32BitValues
			commandList->setGraphicsRootConstant32(0, payloadID, 0);
			updateMaterialCBV(payloadID, &payload, sizeof(payload));
			updateMaterialSRV(commandList, payloadID, section.material);

			commandList->iaSetVertexBuffers(0, 1, &section.positionBuffer);
			commandList->iaSetIndexBuffer(section.indexBuffer);
			commandList->drawIndexedInstanced(section.indexBuffer->getIndexCount(), 1, 0, 0, 0);

			++payloadID;
		}
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
	DescriptorHeap* volatileHeap = volatileViewHeaps[frameIndex].get();
	DescriptorHeap* heaps[] = { volatileHeap, };
	cmdList->setDescriptorHeaps(1, heaps);

	// Scene uniform
	gRenderDevice->copyDescriptors(
		1,
		volatileHeap,
		0,
		sceneUniformHeaps[frameIndex].get(), 0);
	cmdList->setGraphicsRootDescriptorTable(1, volatileHeap, 0);

	// Material CBV
	gRenderDevice->copyDescriptors(
		numPayloads,
		volatileHeap,
		1,
		cbvHeap[frameIndex].get(), 0);
	cmdList->setGraphicsRootDescriptorTable(2, volatileHeap, 1);

	// Material SRV
	// #todo-renderer: SRV won't be updated per drawcall!
	// But unbound CBV is already problematic for HLSL -> SPIR-V translation...
	cmdList->setGraphicsRootDescriptorTable(3, volatileHeap, 1 + numPayloads);
}

void BasePass::updateMaterialCBV(uint32 payloadID, void* payload, uint32 payloadSize)
{
	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	materialConstantBuffers[frameIndex]->upload(payloadID, payload, payloadSize);
}

void BasePass::updateMaterialSRV(RenderCommandList* cmdList, uint32 payloadID, Material* material)
{
	Texture* albedo = gTextureManager->getSystemTextureGrey2D();
	if (material) {
		albedo = material->albedoTexture;
	}

	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	const uint32 numSRVs = 1; // For this drawcall
	DescriptorHeap* volatileHeap = volatileViewHeaps[frameIndex].get();

	uint32 descriptorStartOffset = 1 + numPayloads; // SRVs come right after scene uniform CBV and material CBVs
	descriptorStartOffset += payloadID * numSRVs;
	
	gRenderDevice->copyDescriptors(
		numSRVs,
		volatileHeap, descriptorStartOffset,
		gTextureManager->getSRVHeap(), albedo->getSRVDescriptorIndex());
}
