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
	ShaderStage* depthVS = device->createShader(EShaderStage::VERTEX_SHADER, "DepthPrepassVS");
	ShaderStage* depthPS = device->createShader(EShaderStage::PIXEL_SHADER, "DepthPrepassPS");
	depthVS->declarePushConstants({ { "pushConstants", 1} });
	depthPS->declarePushConstants({ { "pushConstants", 1} });
	depthVS->loadFromFile(L"base_pass.hlsl", "mainVS", { L"DEPTH_PREPASS" });
	depthPS->loadFromFile(L"base_pass.hlsl", "mainPS", { L"DEPTH_PREPASS" });

	ShaderStage* visVS = device->createShader(EShaderStage::VERTEX_SHADER, "DepthAndVisVS");
	ShaderStage* visPS = device->createShader(EShaderStage::PIXEL_SHADER, "DepthAndVisPS");
	visVS->declarePushConstants({ { "pushConstants", 1} });
	visPS->declarePushConstants({ { "pushConstants", 1} });
	visVS->loadFromFile(L"base_pass.hlsl", "mainVS", { L"DEPTH_PREPASS", L"VISIBILITY_BUFFER" });
	visPS->loadFromFile(L"base_pass.hlsl", "mainPS", { L"DEPTH_PREPASS", L"VISIBILITY_BUFFER" });

	ShaderStage* baseVS = device->createShader(EShaderStage::VERTEX_SHADER, "BasePassVS");
	ShaderStage* basePS = device->createShader(EShaderStage::PIXEL_SHADER, "BasePassPS");
	baseVS->declarePushConstants({ { "pushConstants", 1} });
	basePS->declarePushConstants({ { "pushConstants", 1} });
	baseVS->loadFromFile(L"base_pass.hlsl", "mainVS");
	basePS->loadFromFile(L"base_pass.hlsl", "mainPS");

	// For each pipeline key, compile shaders for corresponding render passes.
	for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
	{
		const GraphicsPipelineKeyDesc& keyDesc = GraphicsPipelineKeyDesc::kPipelineKeyDescs[i];
		GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(keyDesc);

		MaterialShaderPasses passes{};
		passes.depthPrepass = createDepthPipeline(device, keyDesc, depthVS, depthPS, false);
		passes.depthAndVisibility = createDepthPipeline(device, keyDesc, visVS, visPS, true);
		passes.basePass = createBasePipeline(device, keyDesc, baseVS, basePS);

		database.push_back({ pipelineKey, passes });
	}

	delete depthVS; delete depthPS;
	delete visVS; delete visPS;
	delete baseVS; delete basePS;
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

const MaterialShaderPasses* MaterialShaderDatabase::findPasses(GraphicsPipelineKey key) const
{
	for (const auto& kv : database)
	{
		if (kv.first == key) return &kv.second;
	}
	return nullptr;
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

	return device->createGraphicsPipelineState(pipelineDesc);
}

GraphicsPipelineState* MaterialShaderDatabase::createBasePipeline(
	RenderDevice* device,
	const GraphicsPipelineKeyDesc& pipelineKeyDesc,
	ShaderStage* vs,
	ShaderStage* ps)
{
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	RasterizerDesc rasterizerDesc = RasterizerDesc();
	rasterizerDesc.cullMode = pipelineKeyDesc.cullMode;

	VertexInputLayout inputLayout = createVertexInputLayout();

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

	const uint32 numRTVs = (uint32)(1 + NUM_GBUFFERS + 1); // sceneColor + gbuffers + velocityMap

	constexpr bool bReverseZ = getReverseZPolicy() == EReverseZPolicy::Reverse;
	DepthstencilDesc depthStencilDesc = bReverseZ
		? DepthstencilDesc::ReverseZSceneDepth()
		: DepthstencilDesc::StandardSceneDepth();
	
	// Change comparison func for depth prepass.
	depthStencilDesc.depthFunc = bReverseZ
		? EComparisonFunc::GreaterEqual
		: EComparisonFunc::LessEqual;

	GraphicsPipelineDesc pipelineDesc{
		.vs                     = vs,
		.ps                     = ps,
		.blendDesc              = BlendDesc(),
		.sampleMask             = 0xffffffff,
		.rasterizerDesc         = std::move(rasterizerDesc),
		.depthstencilDesc       = std::move(depthStencilDesc),
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
	pipelineDesc.rtvFormats[rtvIndex++] = PF_sceneColor;
	for (size_t i = 0; i < NUM_GBUFFERS; ++i)
	{
		pipelineDesc.rtvFormats[rtvIndex++] = PF_gbuffers[i];
	}
	pipelineDesc.rtvFormats[rtvIndex++] = PF_velocityMap;
	CHECK(rtvIndex == numRTVs);

	return device->createGraphicsPipelineState(pipelineDesc);
}
