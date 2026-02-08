#include "depth_prepass.h"
#include "material.h"
#include "static_mesh.h"
#include "gpu_scene.h"
#include "gpu_culling.h"

#include "rhi/render_device.h"
#include "rhi/rhi_policy.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"
#include "rhi/vertex_buffer_pool.h"

// #wip: Remove when switching to MaterialShaderDatabase.
static VertexInputLayout createVertexInputLayout()
{
	// #todo-basepass: Should be variant per vertex factory
	return VertexInputLayout{
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0},
		{"NORMAL", 0, EPixelFormat::R32G32B32_FLOAT, 1, 0, EVertexInputClassification::PerVertex, 0},
		{"TEXCOORD", 0, EPixelFormat::R32G32_FLOAT, 1, sizeof(float) * 3, EVertexInputClassification::PerVertex, 0}
	};
}

DepthPrepass::~DepthPrepass()
{
	delete shaderVS;
	delete shaderPS;
	delete visShaderVS;
	delete visShaderPS;
}

void DepthPrepass::initialize(RenderDevice* inRenderDevice, EPixelFormat inVisBufferFormat)
{
	device = inRenderDevice;
	visBufferFormat = inVisBufferFormat;

	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	passDescriptor.initialize(L"DepthPrepass", swapchainCount, 0);

	// Standard pipeline
	{
		shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "DepthPrepassVS");
		shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "DepthPrepassPS");
		shaderVS->declarePushConstants({ { "pushConstants", 1} });
		shaderPS->declarePushConstants({ { "pushConstants", 1} });
		shaderVS->loadFromFile(L"base_pass.hlsl", "mainVS", { L"DEPTH_PREPASS" });
		shaderPS->loadFromFile(L"base_pass.hlsl", "mainPS", { L"DEPTH_PREPASS" });

		for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
		{
			auto pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
			auto pipelineState = createPipeline(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i], shaderVS, shaderPS, false);

			IndirectDrawHelper* indirectDrawHelper = new(EMemoryTag::Renderer) IndirectDrawHelper;
			indirectDrawHelper->initialize(device, pipelineState, pipelineKey, L"DepthPrepass");

			pipelinePermutation.insertPipeline(pipelineKey, GraphicsPipelineItem{ pipelineState, indirectDrawHelper });
		}
	}
	// Visibility buffer pipeline
	{
		visShaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "DepthPrepassWithVisVS");
		visShaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "DepthPrepassWithVisPS");
		visShaderVS->declarePushConstants({ { "pushConstants", 1} });
		visShaderPS->declarePushConstants({ { "pushConstants", 1} });
		visShaderVS->loadFromFile(L"base_pass.hlsl", "mainVS", { L"DEPTH_PREPASS", L"VISIBILITY_BUFFER" });
		visShaderPS->loadFromFile(L"base_pass.hlsl", "mainPS", { L"DEPTH_PREPASS", L"VISIBILITY_BUFFER" });

		for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
		{
			auto pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
			auto pipelineState = createPipeline(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i], visShaderVS, visShaderPS, true);

			IndirectDrawHelper* indirectDrawHelper = new(EMemoryTag::Renderer) IndirectDrawHelper;
			indirectDrawHelper->initialize(device, pipelineState, pipelineKey, L"DepthPrepassWithVis");

			visPipelinePermutation.insertPipeline(pipelineKey, GraphicsPipelineItem{ pipelineState, indirectDrawHelper });
		}
	}
}

void DepthPrepass::renderDepthPrepass(RenderCommandList* commandList, uint32 swapchainIndex, const DepthPrepassInput& passInput)
{
	if (passInput.gpuScene->getGPUSceneItemMaxCount() == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}

	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = passInput.gpuScene->queryMaterialDescriptors();

	// Bind shader parameters except for root constants.
	// #note: Assumes all permutation share the same root signature.
	{
		auto key = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kDefaultPipelineKeyDesc);
		auto defaultPipeline = passInput.bVisibilityBuffer
			? visPipelinePermutation.findPipeline(key).pipelineState
			: pipelinePermutation.findPipeline(key).pipelineState;

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.structuredBuffer("gpuSceneBuffer", passInput.gpuScene->getGPUSceneBufferSRV());

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
		.psoPermutation = passInput.bVisibilityBuffer ? &visPipelinePermutation : &pipelinePermutation,
	};
	StaticMeshRendering::renderStaticMeshes(commandList, swapchainIndex, meshDrawInput);
}

GraphicsPipelineState* DepthPrepass::createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc, ShaderStage* vs, ShaderStage* ps, bool bUseVisibilityBuffer)
{
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	RasterizerDesc rasterizerDesc = RasterizerDesc();
	rasterizerDesc.cullMode = pipelineKeyDesc.cullMode;

	DepthstencilDesc depthStencilDesc = getReverseZPolicy() == EReverseZPolicy::Reverse
		? DepthstencilDesc::ReverseZSceneDepth()
		: DepthstencilDesc::StandardSceneDepth();

	VertexInputLayout inputLayout = createVertexInputLayout();

	GraphicsPipelineDesc pipelineDesc{
		.vs                     = vs,
		.ps                     = ps,
		.blendDesc              = BlendDesc(),
		.sampleMask             = 0xffffffff,
		.rasterizerDesc         = std::move(rasterizerDesc),
		.depthstencilDesc       = std::move(depthStencilDesc),
		.inputLayout            = inputLayout,
		.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
		.numRenderTargets       = bUseVisibilityBuffer ? 1u : 0u,
		.rtvFormats             = { bUseVisibilityBuffer ? visBufferFormat : EPixelFormat::UNKNOWN },
		.dsvFormat              = swapchain->getBackbufferDepthFormat(),
		.sampleDesc = SampleDesc{
			.count              = swapchain->supports4xMSAA() ? 4u : 1u,
			.quality            = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0,
		},
		.staticSamplers         = {},
	};

	GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(pipelineKeyDesc);
	GraphicsPipelineState* pipelineState = device->createGraphicsPipelineState(pipelineDesc);

	return pipelineState;
}
