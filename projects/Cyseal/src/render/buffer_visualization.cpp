#include "buffer_visualization.h"
#include "renderer_constants.h"

#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/shader.h"

struct PassUniform
{
	uint32 mode; // EBufferVisualizationMode
	uint32 scaledRenderWidth;
	uint32 scaledRenderHeight;
	uint32 indirectDiffuseDebugMode;
	uint32 opticalFlowVectorPackedSize;
};

void BufferVisualization::initialize(RenderDevice* inRenderDevice)
{
	RenderDevice* device = inRenderDevice;
	const uint32 maxFramesInFlight = device->maxFramesInFlight();

	rtvFormats.push_back(PF_finalSceneColor);
	pipelineStates.initialize((uint32)rtvFormats.size());

	passDescriptor.initialize(L"BufferVisualization", maxFramesInFlight, sizeof(PassUniform));

	// Create input layout.
	VertexInputLayout inputLayout = {
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0}
	};

	// Load shaders.
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "BufferVisualizationVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "BufferVisualizationPS");
	shaderVS->declarePushConstants();
	shaderPS->declarePushConstants();
	shaderVS->loadFromFile(L"buffer_visualization.hlsl", "mainVS");
	shaderPS->loadFromFile(L"buffer_visualization.hlsl", "mainPS");

	// Create PSO.
	std::vector<StaticSamplerDesc> staticSamplers = {
		StaticSamplerDesc{
			.name             = "textureSampler",
			.filter           = ETextureFilter::MIN_MAG_MIP_POINT,
			.addressU         = ETextureAddressMode::Clamp,
			.addressV         = ETextureAddressMode::Clamp,
			.addressW         = ETextureAddressMode::Clamp,
			.mipLODBias       = 0.0f,
			.maxAnisotropy    = 0,
			.comparisonFunc   = EComparisonFunc::Always,
			.borderColor      = EStaticBorderColor::OpaqueBlack,
			.minLOD           = 0.0f,
			.maxLOD           = FLT_MAX,
			.shaderVisibility = EShaderVisibility::All,
		},
	};
	for (size_t i = 0; i < rtvFormats.size(); ++i)
	{
		GraphicsPipelineDesc pipelineDesc{
			.vs                     = shaderVS,
			.ps                     = shaderPS,
			.ds                     = nullptr,
			.hs                     = nullptr,
			.gs                     = nullptr,
			.blendDesc              = BlendDesc(),
			.sampleMask             = 0xffffffff,
			.rasterizerDesc         = RasterizerDesc::FrontCull(),
			.depthstencilDesc       = DepthstencilDesc::NoDepth(),
			.inputLayout            = inputLayout,
			.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
			.numRenderTargets       = 1,
			.rtvFormats             = { rtvFormats[i] },
			.dsvFormat              = EPixelFormat::UNKNOWN, // No depth so don't care
			.sampleDesc             = SampleDesc { .count = 1, .quality = 0 },
			.staticSamplers         = staticSamplers,
		};
		pipelineStates[i] = UniquePtr<GraphicsPipelineState>(device->createGraphicsPipelineState(pipelineDesc));
	}

	// Cleanup.
	{
		delete shaderVS;
		delete shaderPS;
	}
}

void BufferVisualization::renderVisualization(RenderCommandList* commandList, const FrameInfo& frameInfo, const BufferVisualizationInput& passInput)
{
	PassUniform uboData{
		.mode                        = (uint32)passInput.mode,
		.scaledRenderWidth           = passInput.scaledRenderWidth,
		.scaledRenderHeight          = passInput.scaledRenderHeight,
		.indirectDiffuseDebugMode    = (uint32)passInput.indirectDiffuseDebugMode,
		.opticalFlowVectorPackedSize = Cymath::packUint16x2(passInput.opticalFlowVectorSizeX, passInput.opticalFlowVectorSizeY),
	};
	auto passUniformCBV = passDescriptor.getUniformCBV(frameInfo);
	passUniformCBV->writeToGPU(commandList, &uboData, sizeof(uboData));
	
	ShaderParameterTable SPT{};
	SPT.constantBuffer("sceneUniform", passInput.sceneUniformCBV);
	SPT.constantBuffer("passUniform", passUniformCBV);
	SPT.texture("gbuffer0", passInput.gbuffer0SRV);
	SPT.texture("gbuffer1", passInput.gbuffer1SRV);
	SPT.texture("sceneColor", passInput.sceneColorSRV); // #todo-renderer: This is not direct lighting anymore ever since CombineLightingPass was introduced.
	SPT.texture("shadowMask", passInput.shadowMaskSRV);
	SPT.texture("indirectDiffuse", passInput.indirectDiffuseSRV);
	SPT.texture("indirectDiffuseMoment", passInput.indirectDiffuseMomentSRV);
	SPT.texture("indirectSpecular", passInput.indirectSpecularSRV);
	SPT.texture("velocityMap", passInput.velocityMapSRV);
	SPT.texture("visibilityBuffer", passInput.visibilityBufferSRV);
	SPT.texture("barycentricCoord", passInput.barycentricCoordSRV);
	SPT.texture("visGBuffer0", passInput.visGbuffer0SRV);
	SPT.texture("visGBuffer1", passInput.visGbuffer1SRV);
	SPT.texture("opticalFlowVectorX", passInput.opticalFlowVectorXSRV);
	SPT.texture("opticalFlowVectorY", passInput.opticalFlowVectorYSRV);
	SPT.texture("interpolatedFrame", passInput.interpolatedFrameSRV);

	GraphicsPipelineState* pipelineState = getPipelineState(passInput.renderTarget);
	DescriptorHeap* volatileHeap = passDescriptor.resizeDescriptorHeap(frameInfo, SPT.totalDescriptors());

	commandList->setGraphicsPipelineState(pipelineState);
	commandList->bindGraphicsShaderParameters(pipelineState, &SPT, volatileHeap);
	commandList->iaSetPrimitiveTopology(EPrimitiveTopology::TRIANGLELIST);

	commandList->beginRenderPass();
	commandList->drawInstanced(3, 1, 0, 0); // Fullscreen triangle
	commandList->endRenderPass();
}

GraphicsPipelineState* BufferVisualization::getPipelineState(Texture* renderTarget) const
{
	CHECK(renderTarget != nullptr);
	for (size_t i = 0; i < rtvFormats.size(); ++i)
	{
		if (rtvFormats[i] == renderTarget->getCreateParams().format)
		{
			return pipelineStates.at(i);
		}
	}
	CHECK_NO_ENTRY();
	return nullptr;
}
