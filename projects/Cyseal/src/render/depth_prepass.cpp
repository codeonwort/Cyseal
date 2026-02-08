#include "depth_prepass.h"
#include "material.h"
#include "static_mesh.h"
#include "gpu_scene.h"
#include "gpu_culling.h"
#include "material/material_database.h"

#include "rhi/render_device.h"
#include "rhi/rhi_policy.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"
#include "rhi/vertex_buffer_pool.h"

void DepthPrepass::initialize(RenderDevice* inRenderDevice, EPixelFormat inVisBufferFormat)
{
	device = inRenderDevice;
	visBufferFormat = inVisBufferFormat;

	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	passDescriptor.initialize(L"DepthPrepass", swapchainCount, 0);

	// Standard pipeline
	for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
	{
		GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
		GraphicsPipelineState* pipelineState = MaterialShaderDatabase::get().findPasses(pipelineKey)->depthPrepass;

		IndirectDrawHelper* indirectDrawHelper = new(EMemoryTag::Renderer) IndirectDrawHelper;
		indirectDrawHelper->initialize(device, pipelineState, pipelineKey, L"DepthPrepass");

		pipelinePermutation.insertPipeline(pipelineKey, GraphicsPipelineItem{ pipelineState, indirectDrawHelper });
	}
	// Visibility buffer pipeline
	for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
	{
		GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
		GraphicsPipelineState* pipelineState = MaterialShaderDatabase::get().findPasses(pipelineKey)->depthAndVisibility;

		IndirectDrawHelper* indirectDrawHelper = new(EMemoryTag::Renderer) IndirectDrawHelper;
		indirectDrawHelper->initialize(device, pipelineState, pipelineKey, L"DepthAndVisibilityPass");

		visPipelinePermutation.insertPipeline(pipelineKey, GraphicsPipelineItem{ pipelineState, indirectDrawHelper });
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
