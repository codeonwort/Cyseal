#include "indirect_diffuse_pass.h"

#include "render/static_mesh.h"
#include "render/gpu_scene.h"
#include "render/bilateral_blur.h"

#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
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
#define PF_momentHistory                    EPixelFormat::R32G32B32A32_UINT

static const int32 BLUR_COUNT = 3;
static float const cPhi       = 1.0f;
static float const nPhi       = 1.0f;
static float const pPhi       = 1.0f;

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
	uint32    screenSize[2];
	float     invScreenSize[2];
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

void IndirectDiffusePass::initialize()
{
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Indirect Diffuse Reflection will be disabled.");
		return;
	}

	initializeRaytracingPipeline();
	initializeTemporalPipeline();
}

void IndirectDiffusePass::initializeRaytracingPipeline()
{
	RenderDevice* device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	rayPassDescriptor.initialize(L"IndirectDiffuse_RayPass", swapchainCount, sizeof(RayPassUniform));

	totalHitGroupShaderRecord.resize(swapchainCount, 0);
	hitGroupShaderTable.initialize(swapchainCount);

	colorHistory.initialize(PF_colorHistory, ETextureAccessFlags::UAV | ETextureAccessFlags::SRV, L"RT_DiffuseColorHistory");
	momentHistory.initialize(PF_momentHistory, ETextureAccessFlags::UAV, L"RT_DiffuseMomentHistory");

	ShaderStage* raygenShader = device->createShader(EShaderStage::RT_RAYGEN_SHADER, "Diffuse_Raygen");
	ShaderStage* closestHitShader = device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "Diffuse_ClosestHit");
	ShaderStage* missShader = device->createShader(EShaderStage::RT_MISS_SHADER, "Diffuse_Miss");
	raygenShader->declarePushConstants();
	closestHitShader->declarePushConstants({ "g_closestHitCB" });
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
	RTPSO = UniquePtr<RaytracingPipelineStateObject>(gRenderDevice->createRaytracingPipelineStateObject(pipelineDesc));

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
	RenderDevice* device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	temporalPassDescriptor.initialize(L"IndirectDiffuse_TemporalPass", swapchainCount, sizeof(TemporalPassUniform));

	ShaderStage* shader = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "IndirectDiffuseTemporalCS");
	shader->declarePushConstants();
	shader->loadFromFile(L"indirect_diffuse_temporal.hlsl", "mainCS");

	std::vector<StaticSamplerDesc> staticSamplers = {
		getLinearSamplerDesc(),
		getPointSamplerDesc(),
	};
	temporalPipeline = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
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
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void IndirectDiffusePass::renderIndirectDiffuse(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectDiffuseInput& passInput)
{
	auto scene               = passInput.scene;
	auto camera              = passInput.camera;
	auto sceneWidth          = passInput.sceneWidth;
	auto sceneHeight         = passInput.sceneHeight;
	auto gpuScene            = passInput.gpuScene;
	auto raytracingScene     = passInput.raytracingScene;
	auto sceneUniformBuffer  = passInput.sceneUniformBuffer;

	if (isAvailable() == false)
	{
		return;
	}
	if (gpuScene->getGPUSceneItemMaxCount() == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}
	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors(swapchainIndex);

	// -------------------------------------------------------------------
	// Phase: Setup

	resizeTextures(commandList, sceneWidth, sceneHeight);

	Texture* currentColorTexture = colorHistory.getTexture(swapchainIndex % 2);
	Texture* prevColorTexture = colorHistory.getTexture((swapchainIndex + 1) % 2);

	auto currentColorUAV = colorHistory.getUAV(swapchainIndex % 2);
	auto prevColorUAV = colorHistory.getUAV((swapchainIndex + 1) % 2);
	auto prevColorSRV = colorHistory.getSRV((swapchainIndex + 1) % 2);

	{
		SCOPED_DRAW_EVENT(commandList, PrevColorBarrier);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, prevColorTexture },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);
	}

	// -------------------------------------------------------------------
	// Phase: Raytracing

	// Update uniforms.
	{
		RayPassUniform* uboData = new RayPassUniform;

		for (uint32 i = 0; i < RANDOM_SEQUENCE_LENGTH; ++i)
		{
			uboData->randFloats0[i] = Cymath::randFloat();
			uboData->randFloats1[i] = Cymath::randFloat();
		}
		uboData->renderTargetWidth = sceneWidth;
		uboData->renderTargetHeight = sceneHeight;
		uboData->frameCounter = frameCounter;
		uboData->mode = (uint32)passInput.mode;

		auto uniformCBV = rayPassDescriptor.getUniformCBV(swapchainIndex);
		uniformCBV->writeToGPU(commandList, uboData, sizeof(RayPassUniform));

		frameCounter = (frameCounter + 1) & 63;

		delete uboData;
	}

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // passUniform
		requiredVolatiles += 1; // gIndexBuffer
		requiredVolatiles += 1; // gVertexBuffer
		requiredVolatiles += 1; // gpuSceneBuffer
		requiredVolatiles += 1; // gpuSceneDesc.constantsBufferSRV
		requiredVolatiles += 1; // rtScene
		requiredVolatiles += 1; // skybox
		requiredVolatiles += 1; // STBN
		requiredVolatiles += 1; // sceneDepthTexture
		requiredVolatiles += 2; // gbuffer0, gbuffer1
		requiredVolatiles += 1; // raytracingTexture
		requiredVolatiles += gpuSceneDesc.srvCount; // albedoTextures[]

		rayPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	}

	// Resize hit group shader table if needed.
	{
		// #todo-lod: Raytracing does not support LOD...
		uint32 requiredRecordCount = scene->totalMeshSectionsLOD0;
		if (requiredRecordCount > totalHitGroupShaderRecord[swapchainIndex])
		{
			resizeHitGroupShaderTable(swapchainIndex, requiredRecordCount);
		}
	}

	commandList->setRaytracingPipelineState(RTPSO.get());

	// Bind global shader parameters.
	{
		DescriptorHeap* volatileHeap = rayPassDescriptor.getDescriptorHeap(swapchainIndex);
		ConstantBufferView* uniformCBV = rayPassDescriptor.getUniformCBV(swapchainIndex);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", sceneUniformBuffer);
		SPT.constantBuffer("passUniform", uniformCBV);
		SPT.accelerationStructure("rtScene", raytracingScene->getSRV());
		SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
		SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("skybox", passInput.skyboxSRV);
		SPT.texture("stbnTexture", stbnSRV.get());
		SPT.texture("gbuffer0", passInput.gbuffer0SRV);
		SPT.texture("gbuffer1", passInput.gbuffer1SRV);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.rwTexture("raytracingTexture", raytracingUAV.get());
		// Bindless
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		commandList->bindRaytracingShaderParameters(RTPSO.get(), &SPT, volatileHeap);
	}
	
	// Dispatch rays and issue barriers.
	{
		SCOPED_DRAW_EVENT(commandList, DiffuseRaytracing);

		DispatchRaysDesc dispatchDesc{
			.raygenShaderTable = raygenShaderTable.get(),
			.missShaderTable   = missShaderTable.get(),
			.hitGroupTable     = hitGroupShaderTable.at(swapchainIndex),
			.width             = sceneWidth,
			.height            = sceneHeight,
			.depth             = 1,
		};
		// #todo-indirect-diffuse: First frame is too yellow in bedroom model? Not a barrier problem?
		commandList->dispatchRays(dispatchDesc);

		TextureMemoryBarrier textureBarriers[] = {
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, raytracingTexture.get() },
		};
		GPUResource* uavBarriers[] = { passInput.indirectDiffuseTexture, raytracingTexture.get() };
		commandList->resourceBarriers(0, nullptr, _countof(textureBarriers), textureBarriers, _countof(uavBarriers), uavBarriers);
	}

	// -------------------------------------------------------------------
	// Phase: Temporal Reconstruction

	// Update uniforms.
	{
		TemporalPassUniform uboData;

		uboData.screenSize[0] = historyWidth;
		uboData.screenSize[1] = historyHeight;
		uboData.invScreenSize[0] = 1.0f / (float)historyWidth;
		uboData.invScreenSize[1] = 1.0f / (float)historyHeight;

		auto uniformCBV = temporalPassDescriptor.getUniformCBV(swapchainIndex);
		uniformCBV->writeToGPU(commandList, &uboData, sizeof(TemporalPassUniform));
	}

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // passUniform
		requiredVolatiles += 1; // sceneDepthTexture
		requiredVolatiles += 1; // raytracingTexture
		requiredVolatiles += 1; // prevSceneDepthTexture
		requiredVolatiles += 1; // prevColorTexture
		requiredVolatiles += 1; // velocityMapTexture
		requiredVolatiles += 1; // currentColorTexture

		temporalPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	}

	// Bind global shader parameters.
	{
		DescriptorHeap* volatileHeap = temporalPassDescriptor.getDescriptorHeap(swapchainIndex);
		ConstantBufferView* uniformCBV = temporalPassDescriptor.getUniformCBV(swapchainIndex);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", sceneUniformBuffer);
		SPT.constantBuffer("passUniform", uniformCBV);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.texture("raytracingTexture", raytracingSRV.get());
		SPT.texture("prevSceneDepthTexture", passInput.prevSceneDepthSRV);
		SPT.texture("prevColorTexture", prevColorSRV);
		SPT.texture("velocityMapTexture", passInput.velocityMapSRV);
		SPT.rwTexture("currentColorTexture", currentColorUAV);

		commandList->setComputePipelineState(temporalPipeline.get());
		commandList->bindComputeShaderParameters(temporalPipeline.get(), &SPT, volatileHeap);
	}

	// Dispatch compute and issue memory barriers.
	{
		SCOPED_DRAW_EVENT(commandList, DiffuseTemporalReprojection);

		uint32 dispatchX = (historyWidth + 7) / 8;
		uint32 dispatchY = (historyHeight + 7) / 8;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		TextureMemoryBarrier textureBarriers[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::UNORDERED_ACCESS, raytracingTexture.get() },
		};
		GPUResource* uavBarriers[] = { currentColorTexture };
		commandList->resourceBarriers(0, nullptr, _countof(textureBarriers), textureBarriers, _countof(uavBarriers), uavBarriers);
	}

	// -------------------------------------------------------------------
	// Phase: Spatial Reconstruction
	
	{
		SCOPED_DRAW_EVENT(commandList, CopyCurrentColorToPrevColor);

		TextureMemoryBarrier barriersBefore[] = {
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::COPY_SRC, currentColorTexture },
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::COPY_DEST, prevColorTexture },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersBefore), barriersBefore);

		commandList->copyTexture2D(currentColorTexture, prevColorTexture);

		TextureMemoryBarrier barriersAfter[] = {
			{ ETextureMemoryLayout::COPY_SRC, ETextureMemoryLayout::UNORDERED_ACCESS, currentColorTexture },
			{ ETextureMemoryLayout::COPY_DEST, ETextureMemoryLayout::UNORDERED_ACCESS, prevColorTexture },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersAfter), barriersAfter);

	}

	BilateralBlurInput blurPassInput{
		.imageWidth      = sceneWidth,
		.imageHeight     = sceneHeight,
		.blurCount       = BLUR_COUNT,
		.cPhi            = cPhi,
		.nPhi            = nPhi,
		.pPhi            = pPhi,
		.sceneUniformCBV = sceneUniformBuffer,
		.inColorTexture  = prevColorTexture,
		.inColorUAV      = prevColorUAV,
		.inSceneDepthSRV = passInput.sceneDepthSRV,
		.inGBuffer0SRV   = passInput.gbuffer0SRV,
		.inGBuffer1SRV   = passInput.gbuffer1SRV,
		.outColorTexture = passInput.indirectDiffuseTexture,
		.outColorUAV     = passInput.indirectDiffuseUAV,
	};
	passInput.bilateralBlur->renderBilateralBlur(commandList, swapchainIndex, blurPassInput);
}

void IndirectDiffusePass::resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight)
{
	if (historyWidth == newWidth && historyHeight == newHeight)
	{
		return;
	}
	historyWidth = newWidth;
	historyHeight = newHeight;

	TextureCreateParams rayTexDesc = TextureCreateParams::texture2D(
		PF_colorHistory, ETextureAccessFlags::SRV | ETextureAccessFlags::UAV, historyWidth, historyHeight);
	raytracingTexture = UniquePtr<Texture>(gRenderDevice->createTexture(rayTexDesc));
	raytracingTexture->setDebugName(L"RT_DiffuseRaytracingTexture");

	raytracingSRV = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(raytracingTexture.get(),
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
	raytracingUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(raytracingTexture.get(),
		UnorderedAccessViewDesc{
			.format         = rayTexDesc.format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	colorHistory.resizeTextures(commandList, historyWidth, historyHeight);
	momentHistory.resizeTextures(commandList, historyWidth, historyHeight);

	{
		SCOPED_DRAW_EVENT(commandList, ColorTextureBarrier);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, raytracingTexture.get() },
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, colorHistory.getTexture(0) },
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, colorHistory.getTexture(1) },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);
	}

	Texture* stbnTexture = gTextureManager->getSTBNVec3Cosine()->getGPUResource().get();
	stbnSRV = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(stbnTexture,
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
		gRenderDevice->createRaytracingShaderTable(RTPSO.get(), maxRecords, sizeof(RootArguments), L"HitGroupShaderTable"));

	for (uint32 i = 0; i < maxRecords; ++i)
	{
		RootArguments rootArguments{
			.pushConstants = ClosestHitPushConstants{ .objectID = i }
		};

		hitGroupShaderTable[swapchainIndex]->uploadRecord(i, INDIRECT_DIFFUSE_HIT_GROUP_NAME, &rootArguments, sizeof(rootArguments));
	}

	CYLOG(LogIndirectDiffuse, Log, L"Resize hit group shader table [%u]: %u records", swapchainIndex, maxRecords);
}
