#include "indirect_diffuse_pass.h"

#include "render/static_mesh.h"
#include "render/gpu_scene.h"
#include "render/bilateral_blur.h"

#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/shader.h"
#include "rhi/texture_manager.h"
#include "rhi/hardware_raytracing.h"

#include "util/logging.h"

// I don't call TraceRays() recursively, so this constant actually doesn't matter.
// Rather see MAX_BOUNCE in indirect_diffuse_raytracing.hlsl.
#define INDIRECT_DIFFUSE_MAX_RECURSION      1
#define INDIRECT_DIFFUSE_HIT_GROUP_NAME     L"IndirectDiffuse_HitGroup"

#define RANDOM_SEQUENCE_LENGTH              (64 * 64)

#define PF_colorHistory                     EPixelFormat::R16G16B16A16_FLOAT
#define PF_momentHistory                    EPixelFormat::R16G16B16A16_FLOAT

static const uint32 MAX_FRAMES_IN_FLIGHT = 2;

static const int32 BLUR_COUNT = 3;

DEFINE_LOG_CATEGORY_STATIC(LogIndirectDiffuse);

struct RayPassUniform
{
	float     randFloats0[RANDOM_SEQUENCE_LENGTH];
	float     randFloats1[RANDOM_SEQUENCE_LENGTH];
	uint32    renderTargetWidth;
	uint32    renderTargetHeight;
	uint32    frameCounter;
	uint32    mode;
};
struct TemporalPassUniform
{
	uint32    prevScreenSize[2];
	float     prevInvScreenSize[2];
};

// Should match with RayPayload in indirect_diffuse_raytracing.hlsl.
struct RayPayload
{
	float  surfaceNormal[3];
	float  roughness;
	float  albedo[3];
	float  hitTime;
	float  emission[3];
	uint32 objectID;
	float  metalMask;
	uint32 _pad[3];
};
// Should match with IntersectionAttributes in indirect_diffuse_raytracing.hlsl.
struct TriangleIntersectionAttributes
{
	float texcoord[2];
};

struct ClosestHitPushConstants
{
	uint32 objectID;
};
static_assert(sizeof(ClosestHitPushConstants) % 4 == 0);

static StaticSamplerDesc getLinearSamplerDesc()
{
	return StaticSamplerDesc{
		.name             = "linearSampler",
		.filter           = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT,
		.addressU         = ETextureAddressMode::Clamp,
		.addressV         = ETextureAddressMode::Clamp,
		.addressW         = ETextureAddressMode::Clamp,
		.mipLODBias       = 0.0f,
		.maxAnisotropy    = 0,
		.comparisonFunc   = EComparisonFunc::Always,
		.borderColor      = EStaticBorderColor::TransparentBlack,
		.minLOD           = 0.0f,
		.maxLOD           = FLT_MAX,
		.shaderVisibility = EShaderVisibility::All,
	};
}
static StaticSamplerDesc getPointSamplerDesc()
{
	return StaticSamplerDesc{
		.name             = "pointSampler",
		.filter           = ETextureFilter::MIN_MAG_MIP_POINT,
		.addressU         = ETextureAddressMode::Clamp,
		.addressV         = ETextureAddressMode::Clamp,
		.addressW         = ETextureAddressMode::Clamp,
		.mipLODBias       = 0.0f,
		.maxAnisotropy    = 0,
		.comparisonFunc   = EComparisonFunc::Always,
		.borderColor      = EStaticBorderColor::TransparentBlack,
		.minLOD           = 0.0f,
		.maxLOD           = FLT_MAX,
		.shaderVisibility = EShaderVisibility::All,
	};
}

void IndirectDiffusePass::initialize(RenderDevice* inDevice)
{
	device = inDevice;
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Indirect Diffuse Reflection will be disabled.");
		return;
	}

	rng = RNG<float>(0.0f, 1.0f);

	initializeRaytracingPipeline();
	initializeTemporalPipeline();
}

void IndirectDiffusePass::initializeRaytracingPipeline()
{
	rayPassDescriptor.initialize(L"IndirectDiffuse_RayPass", MAX_FRAMES_IN_FLIGHT, sizeof(RayPassUniform));

	totalHitGroupShaderRecord.resize(MAX_FRAMES_IN_FLIGHT, 0);
	hitGroupShaderTable.initialize(MAX_FRAMES_IN_FLIGHT);

	colorHistory.initialize(PF_colorHistory, ETextureAccessFlags::UAV | ETextureAccessFlags::SRV, L"RT_DiffuseColorHistory");
	momentHistory.initialize(PF_momentHistory, ETextureAccessFlags::UAV | ETextureAccessFlags::SRV, L"RT_DiffuseMomentHistory");

	ShaderStage* raygenShader = device->createShader(EShaderStage::RT_RAYGEN_SHADER, "Diffuse_Raygen");
	ShaderStage* closestHitShader = device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "Diffuse_ClosestHit");
	ShaderStage* missShader = device->createShader(EShaderStage::RT_MISS_SHADER, "Diffuse_Miss");
	raygenShader->declarePushConstants();
	closestHitShader->declarePushConstants({ { "g_closestHitCB", 1} });
	missShader->declarePushConstants();
	raygenShader->loadFromFile(L"indirect_diffuse_raytracing.hlsl", "MainRaygen");
	closestHitShader->loadFromFile(L"indirect_diffuse_raytracing.hlsl", "MainClosestHit");
	missShader->loadFromFile(L"indirect_diffuse_raytracing.hlsl", "MainMiss");

	// RTPSO
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
		StaticSamplerDesc{
			.name             = "skyboxSampler",
			.filter           = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT,
			.addressU         = ETextureAddressMode::Wrap,
			.addressV         = ETextureAddressMode::Wrap,
			.addressW         = ETextureAddressMode::Wrap,
			.mipLODBias       = 0.0f,
			.maxAnisotropy    = 0,
			.comparisonFunc   = EComparisonFunc::Always,
			.borderColor      = EStaticBorderColor::TransparentBlack,
			.minLOD           = 0.0f,
			.maxLOD           = 0.0f,
			.shaderVisibility = EShaderVisibility::All,
		},
		getLinearSamplerDesc(),
		getPointSamplerDesc(),
	};
	RaytracingPipelineStateObjectDesc pipelineDesc{
		.hitGroupName                 = INDIRECT_DIFFUSE_HIT_GROUP_NAME,
		.hitGroupType                 = ERaytracingHitGroupType::Triangles,
		.raygenShader                 = raygenShader,
		.closestHitShader             = closestHitShader,
		.missShader                   = missShader,
		.raygenLocalParameters        = {},
		.closestHitLocalParameters    = { "g_closestHitCB" },
		.missLocalParameters          = {},
		.maxPayloadSizeInBytes        = sizeof(RayPayload),
		.maxAttributeSizeInBytes      = sizeof(TriangleIntersectionAttributes),
		.maxTraceRecursionDepth       = INDIRECT_DIFFUSE_MAX_RECURSION,
		.staticSamplers               = std::move(staticSamplers),
	};
	RTPSO = UniquePtr<RaytracingPipelineStateObject>(device->createRaytracingPipelineStateObject(pipelineDesc));

	// Raygen shader table
	{
		uint32 numShaderRecords = 1;
		raygenShaderTable = UniquePtr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(RTPSO.get(), numShaderRecords, 0, L"RayGenShaderTable"));
		raygenShaderTable->uploadRecord(0, raygenShader, nullptr, 0);
	}
	// Miss shader table
	{
		uint32 numShaderRecords = 1;
		missShaderTable = UniquePtr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(RTPSO.get(), numShaderRecords, 0, L"MissShaderTable"));
		missShaderTable->uploadRecord(0, missShader, nullptr, 0);
	}
	// Hit group shader table is created in resizeHitGroupShaderTable().
	// ...

	// Cleanup
	{
		delete raygenShader;
		delete closestHitShader;
		delete missShader;
	}
}

void IndirectDiffusePass::initializeTemporalPipeline()
{
	temporalPassDescriptor.initialize(L"IndirectDiffuse_TemporalPass", MAX_FRAMES_IN_FLIGHT, sizeof(TemporalPassUniform));

	ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "IndirectDiffuseTemporalCS");
	shader->declarePushConstants();
	shader->loadFromFile(L"indirect_diffuse_temporal.hlsl", "mainCS");

	std::vector<StaticSamplerDesc> staticSamplers = {
		getLinearSamplerDesc(),
		getPointSamplerDesc(),
	};
	temporalPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
		ComputePipelineDesc{
			.cs             = shader,
			.nodeMask       = 0,
			.staticSamplers = std::move(staticSamplers),
		}
	));

	delete shader;
}

bool IndirectDiffusePass::isAvailable() const
{
	return device->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void IndirectDiffusePass::renderIndirectDiffuse(RenderCommandList* commandList, const FrameInfo& frameInfo, const IndirectDiffuseInput& passInput)
{
	if (isAvailable() == false)
	{
		return;
	}
	if (passInput.gpuScene->getGPUSceneItemMaxCount() == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}

	if (passInput.randomSeed > 0)
	{
		rng.resetSeed(passInput.randomSeed);
	}

	const PassFrameInfo passFrameInfo{
		.currFrame = frameInfo.frameID % 2,
		.prevFrame = (frameInfo.frameID + 1) % 2,
	};

	prepareRaytracingResources(commandList, passFrameInfo, passInput);

	actualHistoryWidth[passFrameInfo.currFrame] = passInput.sceneWidth;
	actualHistoryHeight[passFrameInfo.currFrame] = passInput.sceneHeight;

	raytracingPhase(commandList, passFrameInfo, passInput);
	reprojectPhase(commandList, passFrameInfo, passInput);
	denoisePhase(commandList, frameInfo, passFrameInfo, passInput);
}

void IndirectDiffusePass::resizeTextures(RenderCommandList* commandList, uint32 newUnscaledWidth, uint32 newUnscaledHeight)
{
	if (unscaledHistoryWidth == newUnscaledWidth && unscaledHistoryHeight == newUnscaledHeight)
	{
		return;
	}
	unscaledHistoryWidth = newUnscaledWidth;
	unscaledHistoryHeight = newUnscaledHeight;

	for (uint32 i = 0; i < _countof(actualHistoryWidth); ++i)
	{
		if (actualHistoryWidth[i] == 0 || actualHistoryHeight[i] == 0)
		{
			actualHistoryWidth[i] = unscaledHistoryWidth;
			actualHistoryHeight[i] = unscaledHistoryHeight;
		}
	}

	commandList->enqueueDeferredDealloc(raytracingTexture.release(), true);

	TextureCreateParams rayTexDesc = TextureCreateParams::texture2D(
		PF_colorHistory, ETextureAccessFlags::SRV | ETextureAccessFlags::UAV, unscaledHistoryWidth, unscaledHistoryHeight);
	raytracingTexture = UniquePtr<Texture>(device->createTexture(rayTexDesc));
	raytracingTexture->setDebugName(L"RT_DiffuseRaytracingTexture");

	raytracingSRV = UniquePtr<ShaderResourceView>(device->createSRV(raytracingTexture.get(),
		ShaderResourceViewDesc{
			.format              = rayTexDesc.format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = rayTexDesc.mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	raytracingUAV = UniquePtr<UnorderedAccessView>(device->createUAV(raytracingTexture.get(),
		UnorderedAccessViewDesc{
			.format         = rayTexDesc.format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	colorHistory.resizeTextures(commandList, unscaledHistoryWidth, unscaledHistoryHeight);
	momentHistory.resizeTextures(commandList, unscaledHistoryWidth, unscaledHistoryHeight);

	Texture* stbnTexture = gTextureManager->getSTBNVec3Cosine()->getGPUResource().get();
	stbnSRV = UniquePtr<ShaderResourceView>(device->createSRV(stbnTexture,
		ShaderResourceViewDesc{
			.format              = stbnTexture->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture3D,
			.texture3D           = Texture3DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = stbnTexture->getCreateParams().mipLevels,
				.minLODClamp     = 0.0f,
			},
		}
	));
}

void IndirectDiffusePass::resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords)
{
	totalHitGroupShaderRecord[swapchainIndex] = maxRecords;

	struct RootArguments
	{
		ClosestHitPushConstants pushConstants;
	};

	hitGroupShaderTable[swapchainIndex] = UniquePtr<RaytracingShaderTable>(
		device->createRaytracingShaderTable(RTPSO.get(), maxRecords, sizeof(RootArguments), L"HitGroupShaderTable"));

	for (uint32 i = 0; i < maxRecords; ++i)
	{
		RootArguments rootArguments{
			.pushConstants = ClosestHitPushConstants{ .objectID = i }
		};

		hitGroupShaderTable[swapchainIndex]->uploadRecord(i, INDIRECT_DIFFUSE_HIT_GROUP_NAME, &rootArguments, sizeof(rootArguments));
	}

	CYLOG(LogIndirectDiffuse, Log, L"Resize hit group shader table [%u]: %u records", swapchainIndex, maxRecords);
}

void IndirectDiffusePass::prepareRaytracingResources(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectDiffuseInput& passInput)
{
	const uint32 resourceIndex = passFrameInfo.currFrame;

	resizeTextures(commandList, passInput.unscaledRenderWidth, passInput.unscaledRenderHeight);

	// Resize hit group shader table if needed.
	// #todo-lod: Raytracing does not support LOD...
	uint32 requiredRecordCount = passInput.scene->totalMeshSectionsLOD0;
	if (requiredRecordCount > totalHitGroupShaderRecord[resourceIndex])
	{
		resizeHitGroupShaderTable(resourceIndex, requiredRecordCount);
	}
}

void IndirectDiffusePass::raytracingPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectDiffuseInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, DiffuseRaytracing);

	uint32 sceneWidth = passInput.sceneWidth;
	uint32 sceneHeight = passInput.sceneHeight;
	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = passInput.gpuScene->queryMaterialDescriptors();

	const uint32 currFrame = passFrameInfo.currFrame;
	const uint32 prevFrame = passFrameInfo.prevFrame;

	// Update uniforms.
	{
		RayPassUniform* uboData = new RayPassUniform;

		for (uint32 i = 0; i < RANDOM_SEQUENCE_LENGTH; ++i)
		{
			uboData->randFloats0[i] = rng.get();
			uboData->randFloats1[i] = rng.get();
		}
		uboData->renderTargetWidth = sceneWidth;
		uboData->renderTargetHeight = sceneHeight;
		uboData->frameCounter = frameCounter;
		uboData->mode = (uint32)passInput.mode;

		auto uniformCBV = rayPassDescriptor.getUniformCBV(currFrame);
		uniformCBV->writeToGPU(commandList, uboData, sizeof(RayPassUniform));

		frameCounter = (frameCounter + 1) & 63;

		delete uboData;
	}

	TextureBarrierAuto textureBarriers[] = {
		TextureBarrierAuto::toUnorderedAccess(raytracingTexture.get(), EBarrierSync::COMPUTE_SHADING),
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	// Bind global shader parameters.
	{
		ConstantBufferView* uniformCBV = rayPassDescriptor.getUniformCBV(currFrame);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.constantBuffer("passUniform", uniformCBV);
		SPT.accelerationStructure("rtScene", passInput.raytracingScene->getSRV());
		SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
		SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
		SPT.structuredBuffer("gpuSceneBuffer", passInput.gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("skybox", passInput.skyboxSRV);
		SPT.texture("stbnTexture", stbnSRV.get());
		SPT.texture("gbuffer0", passInput.gbuffer0SRV);
		SPT.texture("gbuffer1", passInput.gbuffer1SRV);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.rwTexture("raytracingTexture", raytracingUAV.get());
		// Bindless
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		uint32 requiredVolatiles = SPT.totalDescriptors();
		DescriptorHeap* volatileHeap = rayPassDescriptor.resizeDescriptorHeap(currFrame, requiredVolatiles);

		commandList->setRaytracingPipelineState(RTPSO.get());
		commandList->bindRaytracingShaderParameters(RTPSO.get(), &SPT, volatileHeap);
	}
	
	DispatchRaysDesc dispatchDesc{
		.raygenShaderTable = raygenShaderTable.get(),
		.missShaderTable   = missShaderTable.get(),
		.hitGroupTable     = hitGroupShaderTable.at(currFrame),
		.width             = sceneWidth,
		.height            = sceneHeight,
		.depth             = 1,
	};
	commandList->dispatchRays(dispatchDesc);
}

void IndirectDiffusePass::reprojectPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectDiffuseInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, DiffuseTemporalReprojection);

	const uint32 currFrame = passFrameInfo.currFrame;
	const uint32 prevFrame = passFrameInfo.prevFrame;

	// Update uniforms.
	{
		TemporalPassUniform uboData;

		uboData.prevScreenSize[0] = actualHistoryWidth[prevFrame];
		uboData.prevScreenSize[1] = actualHistoryHeight[prevFrame];
		uboData.prevInvScreenSize[0] = 1.0f / (float)actualHistoryWidth[prevFrame];
		uboData.prevInvScreenSize[1] = 1.0f / (float)actualHistoryHeight[prevFrame];

		auto uniformCBV = temporalPassDescriptor.getUniformCBV(currFrame);
		uniformCBV->writeToGPU(commandList, &uboData, sizeof(TemporalPassUniform));
	}

	auto currColorTexture = colorHistory.getTexture(currFrame);
	auto prevColorTexture = colorHistory.getTexture(prevFrame);
	auto currColorUAV     = colorHistory.getUAV(currFrame);
	auto prevColorSRV     = colorHistory.getSRV(prevFrame);

	auto currMomentTexture = momentHistory.getTexture(currFrame);
	auto prevMomentTexture = momentHistory.getTexture(prevFrame);
	auto currMomentUAV     = momentHistory.getUAV(currFrame);
	auto prevMomentSRV     = momentHistory.getSRV(prevFrame);

	TextureBarrierAuto textureBarriers[] = {
		TextureBarrierAuto::toShaderResource(prevColorTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(prevMomentTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(raytracingTexture.get(), EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(currColorTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(currMomentTexture, EBarrierSync::COMPUTE_SHADING),
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	// Bind global shader parameters.
	{
		ConstantBufferView* temporalPassUniform = temporalPassDescriptor.getUniformCBV(currFrame);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.constantBuffer("passUniform", temporalPassUniform);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.texture("raytracingTexture", raytracingSRV.get());
		SPT.texture("prevSceneDepthTexture", passInput.prevSceneDepthSRV);
		SPT.texture("prevColorTexture", prevColorSRV);
		SPT.texture("prevMomentTexture", prevMomentSRV);
		SPT.texture("velocityMapTexture", passInput.velocityMapSRV);
		SPT.rwTexture("currentColorTexture", currColorUAV);
		SPT.rwTexture("currentMomentTexture", currMomentUAV);

		uint32 requiredVolatiles = SPT.totalDescriptors();
		DescriptorHeap* volatileHeap = temporalPassDescriptor.resizeDescriptorHeap(currFrame, requiredVolatiles);

		commandList->setComputePipelineState(temporalPipeline.get());
		commandList->bindComputeShaderParameters(temporalPipeline.get(), &SPT, volatileHeap);
	}

	uint32 dispatchX = (passInput.sceneWidth + 7) / 8;
	uint32 dispatchY = (passInput.sceneHeight + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);
}

void IndirectDiffusePass::denoisePhase(RenderCommandList* commandList, const FrameInfo& frameInfo, const PassFrameInfo& passFrameInfo, const IndirectDiffuseInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, DiffuseDenoising);

	const uint32 currFrame = passFrameInfo.currFrame;
	const uint32 prevFrame = passFrameInfo.prevFrame;

	auto currColorTexture = colorHistory.getTexture(currFrame);
	auto currColorUAV     = colorHistory.getUAV(currFrame);

	auto prevColorTexture = colorHistory.getTexture(prevFrame);
	auto prevColorUAV     = colorHistory.getUAV(prevFrame);

#if 1
	{
		SCOPED_DRAW_EVENT(commandList, CopyCurrentColorToPrevColor);

		TextureBarrierAuto barriersBefore[] = {
			TextureBarrierAuto::toCopySource(currColorTexture),
			TextureBarrierAuto::toCopyDest(prevColorTexture),
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		commandList->copyTexture2D(currColorTexture, prevColorTexture);
	}
#endif

	BilateralBlurInput blurPassInput{
		.imageWidth      = passInput.sceneWidth,
		.imageHeight     = passInput.sceneHeight,
		.blurCount       = BLUR_COUNT,
		.cPhi            = passInput.cPhi,
		.nPhi            = passInput.nPhi,
		.pPhi            = passInput.pPhi,
		.sceneUniformCBV = passInput.sceneUniformBuffer,
		.inColorTexture  = prevColorTexture,
		.inColorUAV      = prevColorUAV,
		.inSceneDepthSRV = passInput.sceneDepthSRV,
		.inGBuffer0SRV   = passInput.gbuffer0SRV,
		.inGBuffer1SRV   = passInput.gbuffer1SRV,
		.outColorTexture = passInput.indirectDiffuseTexture,
		.outColorUAV     = passInput.indirectDiffuseUAV,
		//.outColorHistory = prevColorTexture, // #wip: Copy first iteration to prev color
	};
	passInput.bilateralBlur->renderBilateralBlur(commandList, frameInfo, blurPassInput);
}
