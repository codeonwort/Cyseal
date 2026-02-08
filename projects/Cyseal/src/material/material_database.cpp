#include "material_database.h"
#include "material_shader.h"
#include "render/renderer_constants.h"
#include "rhi/render_device.h"
#include "rhi/rhi_policy.h"
#include "rhi/swap_chain.h"

static VertexInputLayout createVertexInputLayout()
{
	// #todo-basepass: Should be variant per vertex factory
	return VertexInputLayout{
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0},
		{"NORMAL", 0, EPixelFormat::R32G32B32_FLOAT, 1, 0, EVertexInputClassification::PerVertex, 0},
		{"TEXCOORD", 0, EPixelFormat::R32G32_FLOAT, 1, sizeof(float) * 3, EVertexInputClassification::PerVertex, 0}
	};
}

MaterialShaderDatabase& MaterialShaderDatabase::get()
{
	static MaterialShaderDatabase instance;
	return instance;
}

void MaterialShaderDatabase::compileMaterials(RenderDevice* device)
{
	// For each pipeline key, compile shaders for corresponding render passes.
	for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
	{
		const GraphicsPipelineKeyDesc& keyDesc = GraphicsPipelineKeyDesc::kPipelineKeyDescs[i];
		GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(keyDesc);

		MaterialShaderPasses passes{};

		// Depth prepass
		{
			ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "DepthPrepassVS");
			ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "DepthPrepassPS");
			shaderVS->declarePushConstants({ { "pushConstants", 1} });
			shaderPS->declarePushConstants({ { "pushConstants", 1} });
			shaderVS->loadFromFile(L"base_pass.hlsl", "mainVS", { L"DEPTH_PREPASS" });
			shaderPS->loadFromFile(L"base_pass.hlsl", "mainPS", { L"DEPTH_PREPASS" });

			passes.depthPrepass = createDepthPipeline(device, keyDesc, shaderVS, shaderPS, false);
			delete shaderVS;
			delete shaderPS;
		}

		database.push_back({ pipelineKey, passes });
	}
}

void MaterialShaderDatabase::destroyMaterials()
{
	for (const auto& kv : database)
	{
		const auto& passes = kv.second;
		if (passes.depthPrepass != nullptr) delete passes.depthPrepass;
		if (passes.depthAndVisibility != nullptr) delete passes.depthAndVisibility;
		if (passes.basePass != nullptr) delete passes.basePass;
	}
	database.clear();
}

GraphicsPipelineState* MaterialShaderDatabase::createDepthPipeline(
	RenderDevice* device,
	const GraphicsPipelineKeyDesc& pipelineKeyDesc,
	ShaderStage* vs,
	ShaderStage* ps,
	bool bUseVisibilityBuffer)
{
	SwapChain* swapchain = device->getSwapChain();

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
		.rtvFormats             = { bUseVisibilityBuffer ? PF_visibilityBuffer : EPixelFormat::UNKNOWN },
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
