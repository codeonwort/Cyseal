#include "base_pass.h"
#include "render_device.h"
#include "swap_chain.h"
#include "gpu_resource_binding.h"
#include "gpu_resource_view.h"
#include "shader.h"
#include "render_command.h"
#include "texture_manager.h"
#include "material.h"
#include "static_mesh.h"
#include "vertex_buffer_pool.h"

#define MAX_STAGING_CBVs                 1024
#define MAX_MATERIAL_CBVs                128          // (Should be >= num meshes in the scene)
#define MAX_VOLATILE_DESCRIPTORS         1024         // (>= scene uniform cbv + material cbvs)
#define CONSTANT_BUFFER_MEMORY_POOL_SIZE (640 * 1024) // 640 KiB

// #todo-renderer: Acquire pixel format from Texture
#define PF_sceneColor            EPixelFormat::R32G32B32A32_FLOAT

struct SceneUniform
{
	Float4x4 viewMatrix;
	Float4x4 projMatrix;
	Float4x4 viewProjMatrix;

	vec3 sunDirection; float _pad0;
	vec3 sunIlluminance; float _pad1;
};

struct MaterialConstants
{
	Float4x4 modelMatrix;
	float albedoMultiplier[4];
};

void BasePass::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	// Create root signature
	// - slot0: 32-bit constant (object id)
	// - slot1: descriptor table (CBV)
	// - slot2: descriptor table (SRV)
	// - slot3: gpu scene (SRV)
	{
		constexpr uint32 NUM_ROOT_PARAMETERS = 5;
		RootParameter slotRootParameters[NUM_ROOT_PARAMETERS];
		
		DescriptorRange descriptorRanges[3];
		descriptorRanges[0].init(EDescriptorRangeType::CBV, 1, 1 /*b1*/);
		descriptorRanges[1].init(EDescriptorRangeType::CBV, 1, 2 /*b2 ~ b?*/);
		descriptorRanges[2].init(EDescriptorRangeType::SRV, 1, 0 /*t0*/);

		slotRootParameters[0].initAsConstants(0 /*b0*/, 0, 1); // object ID
		slotRootParameters[1].initAsDescriptorTable(1, &descriptorRanges[0]); // scene uniform
		// NOTE: Can't group material CBV and SRV into the same table.
		// numPayloads is varying, so setGraphicsRootDescriptorTable()
		// can't decidie where to start SRV descriptors if they are in the same table.
		// #todo-wip: This CBV is replaced with gpu scene
		slotRootParameters[2].initAsDescriptorTable(1, &descriptorRanges[1]); // material CBV
		slotRootParameters[3].initAsDescriptorTable(1, &descriptorRanges[2]); // material SRV
		slotRootParameters[4].initAsSRV(1 /*t1*/, 0);

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

	// Create constant buffer - memory pool, descriptor heap, cbv
	constantBufferMemory = std::unique_ptr<ConstantBuffer>(device->createConstantBuffer(CONSTANT_BUFFER_MEMORY_POOL_SIZE));
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::CBV;
		desc.numDescriptors = MAX_STAGING_CBVs;
		desc.flags = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		cbvStagingHeap = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));
	}
	materialCBVs.resize(MAX_MATERIAL_CBVs);
	for (uint32 i = 0; i < materialCBVs.size(); ++i)
	{
		materialCBVs[i] = std::unique_ptr<ConstantBufferView>(
			constantBufferMemory->allocateCBV(cbvStagingHeap.get(), sizeof(MaterialConstants), swapchainCount));
	}
	sceneUniformCBV = std::unique_ptr<ConstantBufferView>(
		constantBufferMemory->allocateCBV(cbvStagingHeap.get(), sizeof(SceneUniform), swapchainCount));

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
		swprintf_s(debugName, L"BasePass_VolatileViewHeap_%u", i);
		volatileViewHeaps[i]->setDebugName(debugName);
	}

	// Create input layout
	VertexInputLayout inputLayout = {
			{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0},
			{"NORMAL", 0, EPixelFormat::R32G32B32_FLOAT, 1, 0, EVertexInputClassification::PerVertex, 0},
			{"TEXCOORD", 0, EPixelFormat::R32G32_FLOAT, 1, sizeof(float) * 3, EVertexInputClassification::PerVertex, 0}
	};

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
		// #todo-renderer: Delete shader object
		//delete shader;
	}
}

void BasePass::renderBasePass(
	RenderCommandList* commandList,
	const SceneProxy* scene,
	const Camera* camera,
	StructuredBuffer* gpuSceneBuffer)
{
	// #todo-renderer: Support other topologies
	const EPrimitiveTopology primitiveTopology = EPrimitiveTopology::TRIANGLELIST;

	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature
	//   Setting a PSO does not change the root signature.
	//   The application must call a dedicated API for setting the root signature.
	commandList->setPipelineState(pipelineState.get());
	commandList->setGraphicsRootSignature(rootSignature.get());
	
	commandList->iaSetPrimitiveTopology(primitiveTopology);

	// #todo-lod: LOD selection
	const uint32 LOD = 0;

	// #todo-renderer: There might be duplicate descriptors between meshes. Need a drawcall sorting mechanism.
	uint32 numVolatileDescriptors = 0;
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		numVolatileDescriptors += (uint32)mesh->getSections(LOD).size();
	}
	bindRootParameters(commandList, numVolatileDescriptors, gpuSceneBuffer);

	// Update scene uniform buffer
	{
		SceneUniform uboData;
		uboData.viewMatrix = camera->getViewMatrix();
		uboData.projMatrix = camera->getProjMatrix();
		uboData.viewProjMatrix = camera->getViewProjMatrix();
		uboData.sunDirection = scene->sun.direction;
		uboData.sunIlluminance = scene->sun.illuminance;

		const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
		sceneUniformCBV->upload(&uboData, sizeof(uboData), frameIndex);
	}

	uint32 payloadID = 0;
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		for (const StaticMeshSection& section : mesh->getSections(LOD))
		{
			MaterialConstants payload;
			payload.modelMatrix = mesh->getTransform().getMatrix();
			memcpy_s(payload.albedoMultiplier, sizeof(payload.albedoMultiplier),
				section.material->albedoMultiplier, sizeof(section.material->albedoMultiplier));

			// rootParameterIndex, constant, destOffsetIn32BitValues
			commandList->setGraphicsRootConstant32(0, payloadID, 0);
			updateMaterialCBV(payloadID, &payload, sizeof(payload));
			updateMaterialSRV(commandList, numVolatileDescriptors, payloadID, section.material);

			VertexBuffer* vertexBuffers[] = { section.positionBuffer,section.nonPositionBuffer };
			commandList->iaSetVertexBuffers(0, 2, vertexBuffers);
			commandList->iaSetIndexBuffer(section.indexBuffer);
			commandList->drawIndexedInstanced(section.indexBuffer->getIndexCount(), 1, 0, 0, 0);

			++payloadID;
		}
	}
}

void BasePass::bindRootParameters(
	RenderCommandList* cmdList,
	uint32 inNumPayloads,
	StructuredBuffer* gpuSceneBuffer)
{
	CHECK(inNumPayloads <= MAX_VOLATILE_DESCRIPTORS);

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
		volatileHeap, 0,
		cbvStagingHeap.get(), sceneUniformCBV->getDescriptorIndexInHeap(frameIndex));
	cmdList->setGraphicsRootDescriptorTable(1, volatileHeap, 0);

	// Material CBV
	for (uint32 payloadId = 0; payloadId < inNumPayloads; ++payloadId)
	{
		// #todo-wip: Oops... it's actually a bad idea to inject buffering to CBV :/
		// I can't copy all descriptors for the current frame by single copyDescriptors() call.
#if 0
		gRenderDevice->copyDescriptors(
			numPayloads,
			volatileHeap, 1,
			cbvStagingHeap.get(), 0);
#else
		gRenderDevice->copyDescriptors(
			1,
			volatileHeap, 1 + payloadId,
			cbvStagingHeap.get(), materialCBVs[payloadId]->getDescriptorIndexInHeap(frameIndex));
#endif
	}
	cmdList->setGraphicsRootDescriptorTable(2, volatileHeap, 1);

	// Material SRV
	// #todo-wip: SRV won't be updated per drawcall!
	// But unbound CBV is already problematic for HLSL -> SPIR-V translation...
	cmdList->setGraphicsRootDescriptorTable(3, volatileHeap, 1 + inNumPayloads);

	cmdList->setGraphicsRootDescriptorSRV(4, gpuSceneBuffer->getSRV());
}

void BasePass::updateMaterialCBV(uint32 payloadID, void* payload, uint32 payloadSize)
{
	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	materialCBVs[payloadID]->upload(payload, payloadSize, frameIndex);
}

void BasePass::updateMaterialSRV(RenderCommandList* cmdList, uint32 totalPayloads, uint32 payloadID, Material* material)
{
	Texture* albedo = gTextureManager->getSystemTextureGrey2D();
	if (material) {
		albedo = material->albedoTexture;
	}

	const uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	const uint32 numSRVs = 1; // For this drawcall
	DescriptorHeap* volatileHeap = volatileViewHeaps[frameIndex].get();

	uint32 descriptorStartOffset = 1 + totalPayloads; // SRVs come right after scene uniform CBV and material CBVs
	descriptorStartOffset += payloadID * numSRVs;
	
	gRenderDevice->copyDescriptors(
		numSRVs,
		volatileHeap, descriptorStartOffset,
		gTextureManager->getSRVHeap(), albedo->getSRVDescriptorIndex());
}
