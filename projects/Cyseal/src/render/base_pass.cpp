#include "base_pass.h"
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

void BasePass::initialize(RenderDevice* inRenderDevice, EPixelFormat inSceneColorFormat, const EPixelFormat inGbufferFormats[], uint32 numGBuffers, EPixelFormat inVelocityMapFormat)
{
	device = inRenderDevice;
	sceneColorFormat = inSceneColorFormat;
	gbufferFormats.assign(inGbufferFormats, inGbufferFormats + numGBuffers);
	velocityMapFormat = inVelocityMapFormat;

	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	passDescriptor.initialize(L"BasePass", swapchainCount, 0);

	for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
	{
		GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
		GraphicsPipelineState* pipelineState = MaterialShaderDatabase::get().findPasses(pipelineKey)->basePass;

		IndirectDrawHelper* indirectDrawHelper = new(EMemoryTag::Renderer) IndirectDrawHelper;
		indirectDrawHelper->initialize(device, pipelineState, pipelineKey, L"BasePass");

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

	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors();

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
