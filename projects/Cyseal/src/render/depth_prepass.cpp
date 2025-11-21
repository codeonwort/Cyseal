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

DepthPrepass::~DepthPrepass()
{
	delete shaderVS;
	delete shaderPS;
}

void DepthPrepass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;

	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	passDescriptor.initialize(L"DepthPrepass", swapchainCount, 0);

	// Shader stages
	shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "DepthPrepassVS");
	shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "DepthPrepassPS");
	shaderVS->declarePushConstants({ { "pushConstants", 1} });
	shaderPS->declarePushConstants({ { "pushConstants", 1} });
	shaderVS->loadFromFile(L"base_pass.hlsl", "mainVS", { L"DEPTH_PREPASS" });
	shaderPS->loadFromFile(L"base_pass.hlsl", "mainPS", { L"DEPTH_PREPASS" });

	for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
	{
		auto pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
		auto pipelineState = createPipeline(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);

		IndirectDrawHelper* indirectDrawHelper = new(EMemoryTag::Renderer) IndirectDrawHelper;
		indirectDrawHelper->initialize(device, pipelineState, pipelineKey);

		pipelinePermutation.insertPipeline(pipelineKey, GraphicsPipelineItem{ pipelineState, indirectDrawHelper });
	}
}

void DepthPrepass::renderDepthPrepass(RenderCommandList* commandList, uint32 swapchainIndex, const DepthPrepassInput& passInput)
{
	// #wip: Issue drawcall
}

GraphicsPipelineState* DepthPrepass::createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc)
{
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	RasterizerDesc rasterizerDesc = RasterizerDesc();
	rasterizerDesc.cullMode = pipelineKeyDesc.cullMode;

	VertexInputLayout inputLayout = StaticMeshRendering::createVertexInputLayout();

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

	GraphicsPipelineDesc pipelineDesc{
		.vs                     = shaderVS,
		.ps                     = shaderPS,
		.blendDesc              = BlendDesc(),
		.sampleMask             = 0xffffffff,
		.rasterizerDesc         = std::move(rasterizerDesc),
		.depthstencilDesc       = getReverseZPolicy() == EReverseZPolicy::Reverse ? DepthstencilDesc::ReverseZSceneDepth() : DepthstencilDesc::StandardSceneDepth(),
		.inputLayout            = inputLayout,
		.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
		.numRenderTargets       = 0,
		.rtvFormats             = {},
		.dsvFormat              = swapchain->getBackbufferDepthFormat(),
		.sampleDesc = SampleDesc{
			.count              = swapchain->supports4xMSAA() ? 4u : 1u,
			.quality            = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0,
		},
		.staticSamplers         = std::move(staticSamplers),
	};

	GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(pipelineKeyDesc);
	GraphicsPipelineState* pipelineState = device->createGraphicsPipelineState(pipelineDesc);

	return pipelineState;
}
