#include "base_pass.h"
#include "material.h"
#include "static_mesh.h"
#include "gpu_scene.h"
#include "util/logging.h"

#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"
#include "rhi/vertex_buffer_pool.h"

// Force MRT formats for now.
#define PF_sceneColor            EPixelFormat::R32G32B32A32_FLOAT
#define PF_thinGBufferA          EPixelFormat::R16G16B16A16_FLOAT

DEFINE_LOG_CATEGORY_STATIC(LogBasePass);

namespace RootParameters
{
	enum BasePass
	{
		ObjectIDSlot = 0,
		SceneUniformSlot,
		GPUSceneSlot,
		MaterialConstantsSlot,
		MaterialTexturesSlot,
		Count
	};
};

void BasePass::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	// Root signature
	{
		RootParameter rootParameters[RootParameters::Count];
		
		// #todo-vulkan: Careful with HLSL register space used here.
		// See: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#hlsl-register-and-vulkan-binding
		
		DescriptorRange descriptorRanges[3];
		descriptorRanges[0].init(EDescriptorRangeType::CBV, 1,            1, 0); // register(b1, space0)
		descriptorRanges[1].init(EDescriptorRangeType::CBV, (uint32)(-1), 0, 1); // register(b0, space1)
		descriptorRanges[2].init(EDescriptorRangeType::SRV, (uint32)(-1), 0, 1); // register(t0, space1)

		rootParameters[RootParameters::ObjectIDSlot].initAsConstants(0, 0, 1); // register(b0, space0)
		rootParameters[RootParameters::SceneUniformSlot].initAsDescriptorTable(1, &descriptorRanges[0]);
		rootParameters[RootParameters::GPUSceneSlot].initAsSRV(0, 0); // register(t0, space0)
		rootParameters[RootParameters::MaterialConstantsSlot].initAsDescriptorTable(1, &descriptorRanges[1]);
		rootParameters[RootParameters::MaterialTexturesSlot].initAsDescriptorTable(1, &descriptorRanges[2]);

		constexpr uint32 NUM_STATIC_SAMPLERS = 1;
		StaticSamplerDesc staticSamplers[NUM_STATIC_SAMPLERS];

		memset(staticSamplers + 0, 0, sizeof(staticSamplers[0]));
		staticSamplers[0].filter = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT;
		staticSamplers[0].addressU = ETextureAddressMode::Wrap;
		staticSamplers[0].addressV = ETextureAddressMode::Wrap;
		staticSamplers[0].addressW = ETextureAddressMode::Wrap;
		staticSamplers[0].shaderVisibility = EShaderVisibility::Pixel;

		RootSignatureDesc rootSigDesc(
			RootParameters::Count,
			rootParameters,
			NUM_STATIC_SAMPLERS,
			staticSamplers,
			ERootSignatureFlags::AllowInputAssemblerInputLayout);

		rootSignature = std::unique_ptr<RootSignature>(device->createRootSignature(rootSigDesc));
	}

	// Create input layout
	// #todo: Should be variant per vertex factory
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
		desc.numRenderTargets       = 2;
		desc.rtvFormats[0]          = PF_sceneColor;
		desc.rtvFormats[1]          = PF_thinGBufferA;
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

void BasePass::renderBasePass(
	RenderCommandList* commandList,
	const SceneProxy* scene,
	const Camera* camera,
	ConstantBufferView* sceneUniformBuffer,
	GPUScene* gpuScene,
	Texture* RT_sceneColor,
	Texture* RT_thinGBufferA)
{
	// #todo-renderer: Support other topologies
	const EPrimitiveTopology primitiveTopology = EPrimitiveTopology::TRIANGLELIST;

	CHECK(RT_sceneColor->getCreateParams().format == PF_sceneColor);
	CHECK(RT_thinGBufferA->getCreateParams().format == PF_thinGBufferA);

	// Resize volatile heaps if needed.
	{
		uint32 materialCBVCount, materialSRVCount;
		gpuScene->queryMaterialDescriptorsCount(materialCBVCount, materialSRVCount);

		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // scene uniform
		requiredVolatiles += materialCBVCount;
		requiredVolatiles += materialSRVCount;

		if (requiredVolatiles > totalVolatileDescriptors)
		{
			resizeVolatileHeaps(requiredVolatiles);
		}
	}

	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature
	// Setting a PSO does not change the root signature.
	// The application must call a dedicated API for setting the root signature.
	commandList->setPipelineState(pipelineState.get());
	commandList->setGraphicsRootSignature(rootSignature.get());
	
	commandList->iaSetPrimitiveTopology(primitiveTopology);

	// #todo-lod: LOD selection
	const uint32 LOD = 0;

	bindRootParameters(commandList, sceneUniformBuffer, gpuScene);

	// #todo-indirect-draw: Do it
	uint32 payloadID = 0;
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		for (const StaticMeshSection& section : mesh->getSections(LOD))
		{
			commandList->setGraphicsRootConstant32(RootParameters::ObjectIDSlot, payloadID, 0);

			VertexBuffer* vertexBuffers[] = {
				section.positionBuffer->getGPUResource().get(),
				section.nonPositionBuffer->getGPUResource().get()
			};
			auto indexBuffer = section.indexBuffer->getGPUResource().get();

			commandList->iaSetVertexBuffers(0, 2, vertexBuffers);
			commandList->iaSetIndexBuffer(indexBuffer);
			commandList->drawIndexedInstanced(indexBuffer->getIndexCount(), 1, 0, 0, 0);

			++payloadID;
		}
	}
}

void BasePass::bindRootParameters(
	RenderCommandList* cmdList,
	ConstantBufferView* sceneUniform,
	GPUScene* gpuScene)
{
	uint32 frameIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	// slot0: Updated per drawcall, not here
	//cmdList->setGraphicsRootConstant32(0, payloadID, 0);

	// #todo-sampler: volatile sampler heap in the second element
	DescriptorHeap* volatileHeap = volatileViewHeaps[frameIndex].get();
	DescriptorHeap* heaps[] = { volatileHeap, };
	cmdList->setDescriptorHeaps(1, heaps);

	// Scene uniform
	constexpr uint32 sceneUniformDescIx = 0;
	gRenderDevice->copyDescriptors(
		1,
		volatileHeap, sceneUniformDescIx,
		sceneUniform->getSourceHeap(), sceneUniform->getDescriptorIndexInHeap());
	cmdList->setGraphicsRootDescriptorTable(RootParameters::SceneUniformSlot, volatileHeap, sceneUniformDescIx);

	cmdList->setGraphicsRootDescriptorSRV(RootParameters::GPUSceneSlot, gpuScene->getCulledGPUSceneBufferSRV());
	
	// Material CBV and SRV
	uint32 materialCBVBaseIndex, materialCBVCount;
	uint32 materialSRVBaseIndex, materialSRVCount;
	uint32 freeDescriptorIndexAfterMaterials;
	gpuScene->copyMaterialDescriptors(
		volatileHeap, 1,
		materialCBVBaseIndex, materialCBVCount,
		materialSRVBaseIndex, materialSRVCount,
		freeDescriptorIndexAfterMaterials);
	cmdList->setGraphicsRootDescriptorTable(RootParameters::MaterialConstantsSlot, volatileHeap, materialCBVBaseIndex);
	cmdList->setGraphicsRootDescriptorTable(RootParameters::MaterialTexturesSlot, volatileHeap, materialSRVBaseIndex);
}

void BasePass::resizeVolatileHeaps(uint32 maxDescriptors)
{
	totalVolatileDescriptors = maxDescriptors;

	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	volatileViewHeaps.resize(swapchainCount);
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		volatileViewHeaps[i] = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV_SRV_UAV,
				.numDescriptors = maxDescriptors,
				.flags          = EDescriptorHeapFlags::ShaderVisible,
				.nodeMask       = 0,
			}
		));

		wchar_t debugName[256];
		swprintf_s(debugName, L"BasePass_VolatileViewHeap_%u", i);
		volatileViewHeaps[i]->setDebugName(debugName);
	}

	CYLOG(LogBasePass, Log, L"Resize volatile heap: %u descriptors", maxDescriptors);
}
