#include "base_pass.h"
#include "material.h"
#include "static_mesh.h"
#include "gpu_scene.h"
#include "gpu_culling.h"
#include "util/logging.h"

#include "rhi/render_device.h"
#include "rhi/rhi_policy.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"
#include "rhi/vertex_buffer_pool.h"

DEFINE_LOG_CATEGORY_STATIC(LogBasePass);

BasePass::~BasePass()
{
	delete shaderVS;
	delete shaderPS;
}

void BasePass::initialize(RenderDevice* inRenderDevice, EPixelFormat inSceneColorFormat, const EPixelFormat inGbufferFormats[], uint32 numGBuffers, EPixelFormat inVelocityMapFormat)
{
	device = inRenderDevice;
	sceneColorFormat = inSceneColorFormat;
	gbufferFormats.assign(inGbufferFormats, inGbufferFormats + numGBuffers);
	velocityMapFormat = inVelocityMapFormat;

	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	passDescriptor.initialize(L"BasePass", swapchainCount, 0);

	// Shader stages
	shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "BasePassVS");
	shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "BasePassPS");
	shaderVS->declarePushConstants({ { "pushConstants", 1} });
	shaderPS->declarePushConstants({ { "pushConstants", 1} });
	shaderVS->loadFromFile(L"base_pass.hlsl", "mainVS");
	shaderPS->loadFromFile(L"base_pass.hlsl", "mainPS");

	for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
	{
		auto pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
		auto pipelineState = createPipeline(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);

		IndirectDrawHelper* indirectDrawHelper = new(EMemoryTag::Renderer) IndirectDrawHelper;
		indirectDrawHelper->initialize(device, pipelineState, pipelineKey);

		pipelinePermutation.insertPipeline(pipelineKey, GraphicsPipelineItem{ pipelineState, indirectDrawHelper });
	}
}

void BasePass::renderBasePass(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput)
{
	auto scene    = passInput.scene;
	auto gpuScene = passInput.gpuScene;

	if (gpuScene->getGPUSceneItemMaxCount() == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}

	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors(swapchainIndex);

	// Bind shader parameters except for root constants.
	// #note: Assumes all permutation share the same root signature.
	{
		auto key = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kDefaultPipelineKeyDesc);
		auto defaultPipeline = pipelinePermutation.findPipeline(key).pipelineState;

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("shadowMask", passInput.shadowMaskSRV);
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		uint32 requiredVolatiles = SPT.totalDescriptors();
		passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

		DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);
		commandList->bindGraphicsShaderParameters(defaultPipeline, &SPT, volatileHeap);
	}

	StaticMeshRenderingInput meshDrawInput{
		.scene          = passInput.scene,
		.camera         = passInput.camera,
		.bIndirectDraw  = passInput.bIndirectDraw,
		.bGpuCulling    = passInput.bGPUCulling,
		.gpuScene       = passInput.gpuScene,
		.gpuCulling     = passInput.gpuCulling,
		.psoPermutation = &pipelinePermutation,
	};
	StaticMeshRendering::renderStaticMeshes(commandList, swapchainIndex, meshDrawInput);
}

GraphicsPipelineState* BasePass::createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc)
{
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	RasterizerDesc rasterizerDesc = RasterizerDesc();
	rasterizerDesc.cullMode = pipelineKeyDesc.cullMode;

	// Input layout
	// #todo-basepass: Should be variant per vertex factory
	VertexInputLayout inputLayout = {
			{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0},
			{"NORMAL", 0, EPixelFormat::R32G32B32_FLOAT, 1, 0, EVertexInputClassification::PerVertex, 0},
			{"TEXCOORD", 0, EPixelFormat::R32G32_FLOAT, 1, sizeof(float) * 3, EVertexInputClassification::PerVertex, 0}
	};

	std::vector<StaticSamplerDesc> staticSamplers = {
		StaticSamplerDesc{
			.name             = "albedoSampler",
			.filter           = ETextureFilter::MIN_MAG_MIP_LINEAR,
			.addressU         = ETextureAddressMode::Wrap,
			.addressV         = ETextureAddressMode::Wrap,
			.addressW         = ETextureAddressMode::Wrap,
			.mipLODBias       = 0.0f,
			.maxAnisotropy    = 0,
			.comparisonFunc   = EComparisonFunc::Always,
			.borderColor      = EStaticBorderColor::TransparentBlack,
			.minLOD           = 0.0f,
			.maxLOD           = FLT_MAX,
			.shaderVisibility = EShaderVisibility::All,
		},
	};

	const uint32 numRTVs = (uint32)(1 + gbufferFormats.size() + 1); // sceneColor + gbuffers + velocityMap
	GraphicsPipelineDesc pipelineDesc{
		.vs                     = shaderVS,
		.ps                     = shaderPS,
		.blendDesc              = BlendDesc(),
		.sampleMask             = 0xffffffff,
		.rasterizerDesc         = std::move(rasterizerDesc),
		.depthstencilDesc       = getReverseZPolicy() == EReverseZPolicy::Reverse ? DepthstencilDesc::ReverseZSceneDepth() : DepthstencilDesc::StandardSceneDepth(),
		.inputLayout            = inputLayout,
		.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
		.numRenderTargets       = numRTVs,
		.rtvFormats             = { EPixelFormat::UNKNOWN, }, // Fill later
		.dsvFormat              = swapchain->getBackbufferDepthFormat(),
		.sampleDesc = SampleDesc{
			.count              = swapchain->supports4xMSAA() ? 4u : 1u,
			.quality            = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0,
		},
		.staticSamplers         = std::move(staticSamplers),
	};
	uint32 rtvIndex = 0;
	pipelineDesc.rtvFormats[rtvIndex++] = sceneColorFormat;
	for (size_t i = 0; i < gbufferFormats.size(); ++i)
	{
		pipelineDesc.rtvFormats[rtvIndex++] = gbufferFormats[i];
	}
	pipelineDesc.rtvFormats[rtvIndex++] = velocityMapFormat;
	CHECK(rtvIndex == numRTVs);

	GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(pipelineKeyDesc);
	GraphicsPipelineState* pipelineState = device->createGraphicsPipelineState(pipelineDesc);

	return pipelineState;
}
