#include "indirect_specular_pass.h"

#include "render/static_mesh.h"
#include "render/gpu_scene.h"

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

// Reference: 'D3D12RaytracingHelloWorld' and 'D3D12RaytracingSimpleLighting' samples in
// https://github.com/microsoft/DirectX-Graphics-Samples

// I don't call TraceRays() recursively, so this constant actually doesn't matter.
// Rather see MAX_BOUNCE in indirect_specular_reflection.hlsl.
#define INDIRECT_SPECULAR_MAX_RECURSION     2
#define INDIRECT_SPECULAR_HIT_GROUP_NAME    L"IndirectSpecular_HitGroup"

#define RANDOM_SEQUENCE_LENGTH              (64 * 64)

#define PF_raytracing                       EPixelFormat::R16G16B16A16_FLOAT
#define PF_colorHistory                     EPixelFormat::R16G16B16A16_FLOAT
#define PF_momentHistory                    EPixelFormat::R16G16B16A16_FLOAT

DEFINE_LOG_CATEGORY_STATIC(LogIndirectSpecular);

struct RayPassUniform
{
	float       randFloats0[RANDOM_SEQUENCE_LENGTH];
	float       randFloats1[RANDOM_SEQUENCE_LENGTH];
	uint32      renderTargetWidth;
	uint32      renderTargetHeight;
	uint32      traceMode;
	uint32      _pad0;
};
struct TemporalPassUniform
{
	uint32   screenSize[2];
	float    invScreenSize[2];
	uint32   bInvalidateHistory;
	uint32   bLimitHistory;
	uint32   _pad0;
	uint32   _pad1;
};

// Just to calculate size in bytes.
// Should match with RayPayload in indirect_specular_reflection.hlsl.
struct RayPayload
{
	float  surfaceNormal[3];
	float  roughness;
	float  albedo[3];
	float  hitTime;
	float  emission[3];
	uint32 objectID;
	float  metalMask;
	uint32 materialID;
	float  indexOfRefraction;
	uint32 _pad0;
	float  transmittance[3];
	uint32 _pad1;
};
// Just to calculate size in bytes.
// Should match with IntersectionAttributes in indirect_specular_reflection.hlsl.
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

void IndirecSpecularPass::initialize()
{
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Indirect Specular Reflection will be disabled.");
		return;
	}

	initializeRaytracingPipeline();
	initializeTemporalPipeline();
}

bool IndirecSpecularPass::isAvailable() const
{
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void IndirecSpecularPass::renderIndirectSpecular(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput)
{
	auto scene               = passInput.scene;
	auto sceneWidth          = passInput.sceneWidth;
	auto sceneHeight         = passInput.sceneHeight;

	if (isAvailable() == false)
	{
		return;
	}
	if (passInput.gpuScene->getGPUSceneItemMaxCount() == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}
	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = passInput.gpuScene->queryMaterialDescriptors(swapchainIndex);

	// -------------------------------------------------------------------
	// Phase: Setup

	resizeTextures(commandList, sceneWidth, sceneHeight);

	const uint32 currFrame = swapchainIndex % 2;
	const uint32 prevFrame = (swapchainIndex + 1) % 2;

	auto currColorTexture  = colorHistory.getTexture(currFrame);
	auto prevColorTexture  = colorHistory.getTexture(prevFrame);
	auto currColorUAV      = colorHistory.getUAV(currFrame);
	auto prevColorSRV      = colorHistory.getSRV(prevFrame);

	auto currMomentTexture = momentHistory.getTexture(currFrame);
	auto prevMomentTexture = momentHistory.getTexture(prevFrame);
	auto currMomentUAV     = momentHistory.getUAV(currFrame);
	auto prevMomentSRV     = momentHistory.getSRV(prevFrame);

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
		uboData->traceMode = (uint32)passInput.mode;

		auto uniformCBV = rayPassDescriptor.getUniformCBV(swapchainIndex);
		uniformCBV->writeToGPU(commandList, uboData, sizeof(RayPassUniform));

		delete uboData;
	}

	// -------------------------------------------------------------------
	// Phase: Raytracing

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // passUniform
		requiredVolatiles += 1; // rtScene
		requiredVolatiles += 1; // gIndexBuffer
		requiredVolatiles += 1; // gVertexBuffer
		requiredVolatiles += 1; // gpuSceneBuffer
		requiredVolatiles += 1; // skybox
		requiredVolatiles += 1; // sceneDepthTexture
		requiredVolatiles += 2; // gbuffer0, gbuffer1
		requiredVolatiles += 1; // raytracingTexture
		requiredVolatiles += 1; // gpuSceneDesc.constantsBufferSRV
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
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.constantBuffer("passUniform", uniformCBV);
		SPT.accelerationStructure("rtScene", passInput.raytracingScene->getSRV());
		SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
		SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
		SPT.structuredBuffer("gpuSceneBuffer", passInput.gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("skybox", passInput.skyboxSRV);
		SPT.texture("gbuffer0", passInput.gbuffer0SRV);
		SPT.texture("gbuffer1", passInput.gbuffer1SRV);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.rwTexture("raytracingTexture", raytracingUAV.get());
		// Bindless
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		commandList->bindRaytracingShaderParameters(RTPSO.get(), &SPT, volatileHeap);
	}
	
	DispatchRaysDesc dispatchDesc{
		.raygenShaderTable = raygenShaderTable.get(),
		.missShaderTable   = missShaderTable.get(),
		.hitGroupTable     = hitGroupShaderTable.at(swapchainIndex),
		.width             = sceneWidth,
		.height            = sceneHeight,
		.depth             = 1,
	};
	commandList->dispatchRays(dispatchDesc);

	{
		SCOPED_DRAW_EVENT(commandList, BarriersAfterRaytracing);

		TextureMemoryBarrier textureBarriers[] = {
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, prevColorTexture },
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, prevMomentTexture },
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, raytracingTexture.get() },
		};
		GPUResource* uavBarriers[] = { raytracingTexture.get() };
		commandList->resourceBarriers(
			0, nullptr,
			_countof(textureBarriers), textureBarriers,
			_countof(uavBarriers), uavBarriers);
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
		uboData.bInvalidateHistory = (passInput.mode == EIndirectSpecularMode::ForceMirror);
		uboData.bLimitHistory = (passInput.mode == EIndirectSpecularMode::BRDF);

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
		requiredVolatiles += 1; // velocityMapTexture
		requiredVolatiles += 1; // prevSceneDepthTexture
		requiredVolatiles += 1; // prevColorTexture
		requiredVolatiles += 1; // prevMomentTexture
		requiredVolatiles += 1; // currentColorTexture
		requiredVolatiles += 1; // currentMomentTexture

		temporalPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	}

	// Bind global shader parameters.
	{
		DescriptorHeap* volatileHeap = temporalPassDescriptor.getDescriptorHeap(swapchainIndex);
		ConstantBufferView* uniformCBV = temporalPassDescriptor.getUniformCBV(swapchainIndex);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.constantBuffer("passUniform", uniformCBV);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.texture("raytracingTexture", raytracingSRV.get());
		SPT.texture("prevSceneDepthTexture", passInput.prevSceneDepthSRV);
		SPT.texture("prevColorTexture", prevColorSRV);
		SPT.texture("prevMomentTexture", prevMomentSRV);
		SPT.texture("velocityMapTexture", passInput.velocityMapSRV);
		SPT.rwTexture("currentColorTexture", currColorUAV);
		SPT.rwTexture("currentMomentTexture", currMomentUAV);

		commandList->setComputePipelineState(temporalPipeline.get());
		commandList->bindComputeShaderParameters(temporalPipeline.get(), &SPT, volatileHeap);
	}

	// Dispatch compute and issue memory barriers.
	{
		SCOPED_DRAW_EVENT(commandList, TemporalReprojection);

		uint32 dispatchX = (historyWidth + 7) / 8;
		uint32 dispatchY = (historyHeight + 7) / 8;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		TextureMemoryBarrier textureBarriers[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::UNORDERED_ACCESS, raytracingTexture.get() },
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::UNORDERED_ACCESS, prevColorTexture },
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::UNORDERED_ACCESS, prevMomentTexture },
		};
		GPUResource* uavBarriers[] = { currColorTexture, currMomentTexture };
		commandList->resourceBarriers(0, nullptr, _countof(textureBarriers), textureBarriers, _countof(uavBarriers), uavBarriers);
	}

	// -------------------------------------------------------------------
	// Phase: Spatial Reconstruction

	// #todo-specular: Spatial filter
	{
		SCOPED_DRAW_EVENT(commandList, CopyCurrentColorToSceneColor);

		TextureMemoryBarrier barriersBefore[] = {
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::COPY_SRC, currColorTexture },
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::COPY_DEST, passInput.indirectSpecularTexture },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersBefore), barriersBefore);

		commandList->copyTexture2D(currColorTexture, passInput.indirectSpecularTexture);

		TextureMemoryBarrier barriersAfter[] = {
			{ ETextureMemoryLayout::COPY_SRC, ETextureMemoryLayout::UNORDERED_ACCESS, currColorTexture },
			{ ETextureMemoryLayout::COPY_DEST, ETextureMemoryLayout::UNORDERED_ACCESS, passInput.indirectSpecularTexture },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersAfter), barriersAfter);
	}
}

void IndirecSpecularPass::initializeRaytracingPipeline()
{
	RenderDevice* const device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	rayPassDescriptor.initialize(L"IndirectSpecular_RayPass", swapchainCount, sizeof(RayPassUniform));

	colorHistory.initialize(PF_colorHistory, ETextureAccessFlags::UAV | ETextureAccessFlags::SRV, L"RT_SpecularColorHistory");
	momentHistory.initialize(PF_momentHistory, ETextureAccessFlags::UAV | ETextureAccessFlags::SRV, L"RT_SpecularMomentHistory");

	totalHitGroupShaderRecord.resize(swapchainCount, 0);
	hitGroupShaderTable.initialize(swapchainCount);

	ShaderStage* raygenShader = device->createShader(EShaderStage::RT_RAYGEN_SHADER, "RTR_Raygen");
	ShaderStage* closestHitShader = device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "RTR_ClosestHit");
	ShaderStage* missShader = device->createShader(EShaderStage::RT_MISS_SHADER, "RTR_Miss");
	raygenShader->declarePushConstants();
	closestHitShader->declarePushConstants({ "g_closestHitCB" });
	missShader->declarePushConstants();
	raygenShader->loadFromFile(L"indirect_specular_reflection.hlsl", "MainRaygen");
	closestHitShader->loadFromFile(L"indirect_specular_reflection.hlsl", "MainClosestHit");
	missShader->loadFromFile(L"indirect_specular_reflection.hlsl", "MainMiss");

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
	};
	RaytracingPipelineStateObjectDesc pipelineDesc{
		.hitGroupName                 = INDIRECT_SPECULAR_HIT_GROUP_NAME,
		.hitGroupType                 = ERaytracingHitGroupType::Triangles,
		.raygenShader                 = raygenShader,
		.closestHitShader             = closestHitShader,
		.missShader                   = missShader,
		.raygenLocalParameters        = {},
		.closestHitLocalParameters    = { "g_closestHitCB" },
		.missLocalParameters          = {},
		.maxPayloadSizeInBytes        = sizeof(RayPayload),
		.maxAttributeSizeInBytes      = sizeof(TriangleIntersectionAttributes),
		.maxTraceRecursionDepth       = INDIRECT_SPECULAR_MAX_RECURSION,
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
	delete raygenShader;
	delete closestHitShader;
	delete missShader;
}

void IndirecSpecularPass::initializeTemporalPipeline()
{
	RenderDevice* device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	temporalPassDescriptor.initialize(L"IndirectSpecular_TemporalPass", swapchainCount, sizeof(TemporalPassUniform));

	ShaderStage* shader = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "IndirectSpecularTemporalCS");
	shader->declarePushConstants();
	shader->loadFromFile(L"indirect_specular_temporal.hlsl", "mainCS");

	temporalPipeline = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
		ComputePipelineDesc{
			.cs             = shader,
			.nodeMask       = 0,
			.staticSamplers = { getLinearSamplerDesc(), getPointSamplerDesc() },
		}
	));

	delete shader;
}

void IndirecSpecularPass::resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight)
{
	if (historyWidth == newWidth && historyHeight == newHeight)
	{
		return;
	}
	historyWidth = newWidth;
	historyHeight = newHeight;

	colorHistory.resizeTextures(commandList, historyWidth, historyHeight);
	momentHistory.resizeTextures(commandList, historyWidth, historyHeight);

	commandList->enqueueDeferredDealloc(raytracingTexture.release(), true);

	TextureCreateParams rayTexDesc = TextureCreateParams::texture2D(
		PF_raytracing, ETextureAccessFlags::SRV | ETextureAccessFlags::UAV, historyWidth, historyHeight);

	raytracingTexture = UniquePtr<Texture>(gRenderDevice->createTexture(rayTexDesc));
	raytracingTexture->setDebugName(L"RT_SpecularRaysTexture");

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
			.format = rayTexDesc.format,
			.viewDimension = EUAVDimension::Texture2D,
			.texture2D = Texture2DUAVDesc{.mipSlice = 0, .planeSlice = 0 },
		}
	));

	{
		SCOPED_DRAW_EVENT(commandList, SpecularTexturesInitBarrier);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, raytracingTexture.get() },
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, colorHistory.getTexture(0) },
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, colorHistory.getTexture(1) },
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, momentHistory.getTexture(0) },
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, momentHistory.getTexture(1) },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);
	}
}

void IndirecSpecularPass::resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords)
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

		hitGroupShaderTable[swapchainIndex]->uploadRecord(i, INDIRECT_SPECULAR_HIT_GROUP_NAME, &rootArguments, sizeof(rootArguments));
	}

	CYLOG(LogIndirectSpecular, Log, L"Resize hit group shader table [%u]: %u records", swapchainIndex, maxRecords);
}
