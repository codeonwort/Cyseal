#include "indirect_specular_pass.h"

#include "render/static_mesh.h"
#include "render/gpu_scene.h"

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

// #todo-specular: AMD reflection denoiser is even more broken with render resolution scaling.
// But it didn't work well at 100% scale anyway, so let's revisit later.

// Reference: 'D3D12RaytracingHelloWorld' and 'D3D12RaytracingSimpleLighting' samples in
// https://github.com/microsoft/DirectX-Graphics-Samples

// I don't call TraceRays() recursively, so this constant actually doesn't matter.
// Rather see MAX_BOUNCE in indirect_specular_reflection.hlsl.
#define INDIRECT_SPECULAR_MAX_RECURSION     2
#define INDIRECT_SPECULAR_HIT_GROUP_NAME    L"IndirectSpecular_HitGroup"

#define RANDOM_SEQUENCE_LENGTH              (64 * 64)

#define PF_raytracing                       EPixelFormat::R16G16B16A16_FLOAT
#define PF_colorHistory                     EPixelFormat::R16G16B16A16_FLOAT
#define PF_momentHistory                    EPixelFormat::R16G16_FLOAT
#define PF_sampleCountHistory               EPixelFormat::R16_FLOAT

#define PF_avgRadiance                      EPixelFormat::R16G16B16A16_FLOAT
#define PF_reprojectedRadiance              EPixelFormat::R16G16B16A16_FLOAT
#define PF_amdRadiance                      EPixelFormat::R16G16B16A16_FLOAT
#define PF_amdVariance                      EPixelFormat::R16_FLOAT

// Should match with INDIRECT_DISPATCH_RAYS in shader side.
#define INDIRECT_DISPATCH_RAYS              1

#define USE_AMD_DENOISER                    1

// If 1, AMD denoiser assumes that roughness texture contains perceptual roughness values. See material.h
#define AMD_IS_ROUGHNESS_PERCEPTUAL         0
#define AMD_TEMPORAL_STABILITY_FACTOR       0.7f
// Linear roughness 0.22 ~= percetual roughness 0.47
#define AMD_ROUGHNESS_THRESHOLD             0.22f

static const uint32 MAX_FRAMES_IN_FLIGHT = 2;

DEFINE_LOG_CATEGORY_STATIC(LogIndirectSpecular);

struct RayPassUniform
{
	float    randFloats0[RANDOM_SEQUENCE_LENGTH];
	float    randFloats1[RANDOM_SEQUENCE_LENGTH];
	uint32   renderTargetWidth;
	uint32   renderTargetHeight;
	uint32   traceMode;
	uint32   _pad0;
};
struct TemporalPassUniform
{
	uint32   screenSize[2];
	uint32   prevScreenSize[2];
	float    prevInvScreenSize[2];
	uint32   bInvalidateHistory;
	uint32   bLimitHistory;
	uint32   _pad0;
	uint32   _pad1;
};

// cbuffer cbDenoiserReflections in ffx_denoiser_reflections_callbacks_hlsl.h
struct AMDDenoiserUniform
{
	Float4x4 invProjection;
	Float4x4 invView;
	Float4x4 prevViewProjection;
	uint32   renderSize[2];
	float    invRenderSize[2];
	float    motionVectorScale[2];
	float    normalsUnpackMul;
	float    normalsUnpackAdd;
	uint32   isRoughnessPerceptual;
	float    temporalStabilityFactor;
	float    roughnessThreshold;
	float    _pad0;
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
static StaticSamplerDesc getAmdLinearSamplerDesc()
{
	return StaticSamplerDesc{
		.name             = "s_LinearSampler",
		.filter           = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT,
		.addressU         = ETextureAddressMode::Clamp,
		.addressV         = ETextureAddressMode::Clamp,
		.addressW         = ETextureAddressMode::Clamp,
		.mipLODBias       = 0.0f,
		.maxAnisotropy    = 16,
		.comparisonFunc   = EComparisonFunc::Never,
		.borderColor      = EStaticBorderColor::TransparentBlack,
		.minLOD           = 0.0f,
		.maxLOD           = FLT_MAX,
		.shaderVisibility = EShaderVisibility::All,
	};
}

void IndirecSpecularPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Indirect Specular Reflection will be disabled.");
		return;
	}

	rng = RNG<float>(0.0f, 1.0f);

	initializeClassifierPipeline();
	initializeRaytracingPipeline();
	initializeTemporalPipeline();

	initializeAMDReflectionDenoiser();
	initializeAMDFinalizeColor();
}

bool IndirecSpecularPass::isAvailable() const
{
	return device->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void IndirecSpecularPass::renderIndirectSpecular(RenderCommandList* commandList, const FrameInfo& frameInfo, const IndirectSpecularInput& passInput)
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

	auto currColorTexture  = colorHistory.getTexture(passFrameInfo.currFrame);
	auto prevColorTexture  = colorHistory.getTexture(passFrameInfo.prevFrame);
	auto currMomentTexture = momentHistory.getTexture(passFrameInfo.currFrame);
	auto prevMomentTexture = momentHistory.getTexture(passFrameInfo.prevFrame);

	classifierPhase(commandList, passFrameInfo, passInput);

	raytracingPhase(commandList, passFrameInfo, passInput);

#if !USE_AMD_DENOISER
	legacyDenoisingPhase(commandList, passFrameInfo, passInput);
	{
		SCOPED_DRAW_EVENT(commandList, CopyCurrentColorToSceneColor);

		TextureBarrierAuto barriersBefore[] = {
			TextureBarrierAuto::toCopySource(currColorTexture),
			TextureBarrierAuto::toCopyDest(passInput.indirectSpecularTexture),
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		commandList->copyTexture2D(currColorTexture, passInput.indirectSpecularTexture);
	}
#else
	amdReprojPhase(commandList, passFrameInfo, passInput);
	amdPrefilterPhase(commandList, passFrameInfo, passInput);
	amdResolveTemporalPhase(commandList, passFrameInfo, passInput);
	amdFinalizeOutputPhase(commandList, passFrameInfo, passInput);
	{
		SCOPED_DRAW_EVENT(commandList, CopyCurrentColorToSceneColor);

		// It ended up that amdRadianceHistory.getTexture(prevFrame) stores the denoising result... oops :p
		//auto amdResult = amdRadianceHistory.getTexture(prevFrame);
		auto amdResult = amdFinalColorTexture.get();

		TextureBarrierAuto barriersBefore[] = {
			TextureBarrierAuto::toCopySource(amdResult),
			TextureBarrierAuto::toCopyDest(passInput.indirectSpecularTexture),
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		commandList->copyTexture2D(amdResult, passInput.indirectSpecularTexture);
	}
#endif
}

void IndirecSpecularPass::initializeClassifierPipeline()
{
	classifierPassDescriptor.initialize(L"IndirectSpecular_ClassifierPass", MAX_FRAMES_IN_FLIGHT, 0);
	indirectRaysPassDescriptor.initialize(L"IndirectSpecular_PrepareDispatch", MAX_FRAMES_IN_FLIGHT, 0);

	ShaderStage* tileShader = device->createShader(EShaderStage::COMPUTE_SHADER, "IndirectSpecularClassifierCS");
	tileShader->declarePushConstants({ {"pushConstants", 1} });
	tileShader->loadFromFile(L"indirect_specular_classifier.hlsl", "tileClassificationCS");

	ShaderStage* prepareShader = device->createShader(EShaderStage::COMPUTE_SHADER, "IndirectSpecularIndirectRaysCS");
	prepareShader->declarePushConstants();
	prepareShader->loadFromFile(L"indirect_specular_classifier.hlsl", "prepareIndirectRaysCS");

	classifierPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
		ComputePipelineDesc{
			.cs             = tileShader,
			.nodeMask       = 0,
			.staticSamplers = {},
		}
	));

	indirectRaysPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
		ComputePipelineDesc{
			.cs             = prepareShader,
			.nodeMask       = 0,
			.staticSamplers = {},
		}
	));

	delete tileShader;
	delete prepareShader;
}

void IndirecSpecularPass::initializeRaytracingPipeline()
{
	rayPassDescriptor.initialize(L"IndirectSpecular_RayPass", MAX_FRAMES_IN_FLIGHT, sizeof(RayPassUniform));

	colorHistory.initialize(PF_colorHistory, ETextureAccessFlags::UAV | ETextureAccessFlags::SRV, L"RT_SpecularColorHistory");
	momentHistory.initialize(PF_momentHistory, ETextureAccessFlags::UAV | ETextureAccessFlags::SRV, L"RT_SpecularMomentHistory");
	sampleCountHistory.initialize(PF_sampleCountHistory, ETextureAccessFlags::UAV | ETextureAccessFlags::SRV, L"RT_SpecularSampleCountHistory");

	totalHitGroupShaderRecord.resize(MAX_FRAMES_IN_FLIGHT, 0);
	hitGroupShaderTable.initialize(MAX_FRAMES_IN_FLIGHT);

	ShaderStage* raygenShader = device->createShader(EShaderStage::RT_RAYGEN_SHADER, "RTR_Raygen");
	ShaderStage* closestHitShader = device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "RTR_ClosestHit");
	ShaderStage* missShader = device->createShader(EShaderStage::RT_MISS_SHADER, "RTR_Miss");
	raygenShader->declarePushConstants();
	closestHitShader->declarePushConstants({ { "g_closestHitCB", 1} });
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
	delete raygenShader;
	delete closestHitShader;
	delete missShader;

	// Indirect dispatch
	CommandSignatureDesc commandSignatureDesc{
		.argumentDescs = { IndirectArgumentDesc{ .type = EIndirectArgumentType::DISPATCH_RAYS, }, },
		.nodeMask = 0,
	};
	// Pass null instead of RTPSO because root signature won't be changed by indirect commands.
	rayCommandSignature = UniquePtr<CommandSignature>(device->createCommandSignature(commandSignatureDesc, (RaytracingPipelineStateObject*)nullptr));
	rayCommandGenerator = UniquePtr<IndirectCommandGenerator>(device->createIndirectCommandGenerator(commandSignatureDesc, 1));
	rayCommandBuffer = UniquePtr<Buffer>(device->createBuffer(
		BufferCreateParams{
			.sizeInBytes = rayCommandGenerator->getCommandByteStride(),
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::CPU_WRITE | EBufferAccessFlags::UAV,
		}
	));
	rayCommandBufferUAV = UniquePtr<UnorderedAccessView>(device->createUAV(rayCommandBuffer.get(),
		UnorderedAccessViewDesc{
			.format        = EPixelFormat::UNKNOWN,
			.viewDimension = EUAVDimension::Buffer,
			.buffer        = BufferUAVDesc{
				.firstElement         = 0,
				.numElements          = 1,
				.structureByteStride  = rayCommandGenerator->getCommandByteStride(),
				.counterOffsetInBytes = 0,
				.flags                = EBufferUAVFlags::None,
			},
		}
	));
}

void IndirecSpecularPass::initializeTemporalPipeline()
{
	temporalPassDescriptor.initialize(L"IndirectSpecular_TemporalPass", MAX_FRAMES_IN_FLIGHT, sizeof(TemporalPassUniform));

	ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "IndirectSpecularTemporalCS");
	shader->declarePushConstants();
	shader->loadFromFile(L"indirect_specular_temporal.hlsl", "mainCS");

	temporalPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
		ComputePipelineDesc{
			.cs             = shader,
			.nodeMask       = 0,
			.staticSamplers = { getLinearSamplerDesc(), getPointSamplerDesc() },
		}
	));

	delete shader;
}

void IndirecSpecularPass::initializeAMDReflectionDenoiser()
{
	const auto historyFlags = ETextureAccessFlags::SRV | ETextureAccessFlags::UAV;
	amdRadianceHistory.initialize(PF_amdRadiance, historyFlags, L"RT_SpecularAmdRadianceHistory");
	amdVarianceHistory.initialize(PF_amdVariance, historyFlags, L"RT_SpecularAmdVarianceHistory");
	amdSampleCountHistory.initialize(PF_sampleCountHistory, historyFlags, L"RT_SpecularAmdSampleCountHistory");

	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "AMDSpecularReprojectCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_denoiser_reproject_reflections_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		amdReprojectPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs             = shader,
				.nodeMask       = 0,
				.staticSamplers = { getAmdLinearSamplerDesc() },
			}
		));

		// All 3 passes use the same cbuffer. Let's maintain it in the first pass.
		amdReprojectPassDescriptor.initialize(L"IndirectSpecular_AMDReprojectPass", MAX_FRAMES_IN_FLIGHT, sizeof(AMDDenoiserUniform));

		delete shader;
	}
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "AMDSpecularPrefilterCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_denoiser_prefilter_reflections_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		amdPrefilterPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs             = shader,
				.nodeMask       = 0,
				.staticSamplers = { getAmdLinearSamplerDesc() },
			}
		));

		// cbuffer will be borrowed from first pass.
		amdPrefilterPassDescriptor.initialize(L"IndirectSpecular_AMDPrefilterPass", MAX_FRAMES_IN_FLIGHT, 0);

		delete shader;
	}
	{
		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "AMDSpecularResolveTemporalCS");
		shader->declarePushConstants();
		shader->loadFromFile(L"amd/ffx_denoiser_resolve_temporal_reflections_pass.hlsl", "CS", { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" });

		amdResolveTemporalPipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{
				.cs             = shader,
				.nodeMask       = 0,
				.staticSamplers = { getAmdLinearSamplerDesc() },
			}
		));

		// cbuffer will be borrowed from first pass.
		amdResolveTemporalPassDescriptor.initialize(L"IndirectSpecular_AMDResolveTemporalPass", MAX_FRAMES_IN_FLIGHT, 0);

		delete shader;
	}

	// Indirect dispatch
	CommandSignatureDesc commandSignatureDesc{
		.argumentDescs = { IndirectArgumentDesc{ .type = EIndirectArgumentType::DISPATCH, }, },
		.nodeMask = 0,
	};
	// Pass null instead of pipeline state because root signature won't be changed by indirect commands.
	amdCommandSignature = UniquePtr<CommandSignature>(device->createCommandSignature(commandSignatureDesc, (ComputePipelineState*)nullptr));
	amdCommandGenerator = UniquePtr<IndirectCommandGenerator>(device->createIndirectCommandGenerator(commandSignatureDesc, 1));
	amdCommandBuffer = UniquePtr<Buffer>(device->createBuffer(
		BufferCreateParams{
			.sizeInBytes = amdCommandGenerator->getCommandByteStride(),
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::UAV,
		}
	));
	amdCommandBufferUAV = UniquePtr<UnorderedAccessView>(device->createUAV(amdCommandBuffer.get(),
		UnorderedAccessViewDesc{
			.format        = EPixelFormat::UNKNOWN,
			.viewDimension = EUAVDimension::Buffer,
			.buffer        = BufferUAVDesc{
				.firstElement         = 0,
				.numElements          = 1,
				.structureByteStride  = amdCommandGenerator->getCommandByteStride(),
				.counterOffsetInBytes = 0,
				.flags                = EBufferUAVFlags::None,
			},
		}
	));
}

void IndirecSpecularPass::initializeAMDFinalizeColor()
{
	ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "AMDSpecularFinalizeColorCS");
	shader->declarePushConstants({ {"pushConstants", 2} });
	shader->loadFromFile(L"indirect_specular_amd_temp_finalize.hlsl", "finalizeColorCS");

	amdFinalizePipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
		ComputePipelineDesc{
			.cs             = shader,
			.nodeMask       = 0,
			.staticSamplers = {},
		}
	));

	amdFinalizePassDescriptor.initialize(L"IndirectSpecular_AMDFinalizeColorPass", MAX_FRAMES_IN_FLIGHT, 0);

	delete shader;
}

void IndirecSpecularPass::resizeTextures(RenderCommandList* commandList, uint32 newUnscaledWidth, uint32 newUnscaledHeight)
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

	colorHistory.resizeTextures(commandList, unscaledHistoryWidth, unscaledHistoryHeight);
	momentHistory.resizeTextures(commandList, unscaledHistoryWidth, unscaledHistoryHeight);
	sampleCountHistory.resizeTextures(commandList, unscaledHistoryWidth, unscaledHistoryHeight);

	amdRadianceHistory.resizeTextures(commandList, unscaledHistoryWidth, unscaledHistoryHeight);
	amdVarianceHistory.resizeTextures(commandList, unscaledHistoryWidth, unscaledHistoryHeight);
	amdSampleCountHistory.resizeTextures(commandList, unscaledHistoryWidth, unscaledHistoryHeight);

	TextureCreateParams rayTexDesc = TextureCreateParams::texture2D(
		PF_raytracing, ETextureAccessFlags::SRV | ETextureAccessFlags::UAV, unscaledHistoryWidth, unscaledHistoryHeight);
#if INDIRECT_DISPATCH_RAYS
	// Lazy way to clear the texture.
	rayTexDesc.accessFlags = rayTexDesc.accessFlags | ETextureAccessFlags::RTV;
#endif

	commandList->enqueueDeferredDealloc(raytracingTexture.release(), true);
	raytracingTexture = UniquePtr<Texture>(device->createTexture(rayTexDesc));
	raytracingTexture->setDebugName(L"RT_SpecularRaysTexture");

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
			.format = rayTexDesc.format,
			.viewDimension = EUAVDimension::Texture2D,
			.texture2D = Texture2DUAVDesc{.mipSlice = 0, .planeSlice = 0 },
		}
	));
#if INDIRECT_DISPATCH_RAYS
	raytracingRTV = UniquePtr<RenderTargetView>(device->createRTV(raytracingTexture.get(),
		RenderTargetViewDesc{
			.format            = raytracingTexture->getCreateParams().format,
			.viewDimension     = ERTVDimension::Texture2D,
			.texture2D         = Texture2DRTVDesc{
				.mipSlice      = 0,
				.planeSlice    = 0,
			},
		}
	));
#endif

	commandList->enqueueDeferredDealloc(avgRadianceTexture.release(), true);
	avgRadianceTexture = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			PF_avgRadiance, ETextureAccessFlags::UAV,
			(unscaledHistoryWidth + 7) / 8, (unscaledHistoryHeight + 7) / 8,
			1, 1, 0
		)
	));
	avgRadianceTexture->setDebugName(L"RT_SpecularAvgRadianceTexture");
	avgRadianceSRV = UniquePtr<ShaderResourceView>(device->createSRV(avgRadianceTexture.get(),
		ShaderResourceViewDesc{
			.format              = avgRadianceTexture->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = avgRadianceTexture->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	avgRadianceUAV = UniquePtr<UnorderedAccessView>(device->createUAV(avgRadianceTexture.get(),
		UnorderedAccessViewDesc{
			.format = avgRadianceTexture->getCreateParams().format,
			.viewDimension = EUAVDimension::Texture2D,
			.texture2D = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	commandList->enqueueDeferredDealloc(reprojectedRadianceTexture.release(), true);
	reprojectedRadianceTexture = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			PF_reprojectedRadiance, ETextureAccessFlags::UAV,
			unscaledHistoryWidth, unscaledHistoryHeight,
			1, 1, 0
		)
	));
	reprojectedRadianceTexture->setDebugName(L"RT_SpecularReprojectedRadianceTexture");
	reprojectedRadianceSRV = UniquePtr<ShaderResourceView>(device->createSRV(reprojectedRadianceTexture.get(),
		ShaderResourceViewDesc{
			.format              = reprojectedRadianceTexture->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = reprojectedRadianceTexture->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	reprojectedRadianceUAV = UniquePtr<UnorderedAccessView>(device->createUAV(reprojectedRadianceTexture.get(),
		UnorderedAccessViewDesc{
			.format = reprojectedRadianceTexture->getCreateParams().format,
			.viewDimension = EUAVDimension::Texture2D,
			.texture2D = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	commandList->enqueueDeferredDealloc(amdFinalColorTexture.release(), true);
	amdFinalColorTexture = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			PF_reprojectedRadiance, ETextureAccessFlags::RTV | ETextureAccessFlags::UAV,
			unscaledHistoryWidth, unscaledHistoryHeight,
			1, 1, 0
		)
	));
	amdFinalColorTexture->setDebugName(L"RT_SpecularAMDFinalColorTexture");
	amdFinalColorRTV = UniquePtr<RenderTargetView>(device->createRTV(amdFinalColorTexture.get(),
		RenderTargetViewDesc{
			.format        = amdFinalColorTexture->getCreateParams().format,
			.viewDimension = ERTVDimension::Texture2D,
			.texture2D     = Texture2DRTVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));
	amdFinalColorUAV = UniquePtr<UnorderedAccessView>(device->createUAV(amdFinalColorTexture.get(),
		UnorderedAccessViewDesc{
			.format = amdFinalColorTexture->getCreateParams().format,
			.viewDimension = EUAVDimension::Texture2D,
			.texture2D = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));
}

void IndirecSpecularPass::resizeHitGroupShaderTable(const PassFrameInfo& passFrameInfo, uint32 maxRecords)
{
	const uint32 resourceIndex = passFrameInfo.currFrame;

	totalHitGroupShaderRecord[resourceIndex] = maxRecords;

	struct RootArguments
	{
		ClosestHitPushConstants pushConstants;
	};

	hitGroupShaderTable[resourceIndex] = UniquePtr<RaytracingShaderTable>(
		device->createRaytracingShaderTable(RTPSO.get(), maxRecords, sizeof(RootArguments), L"HitGroupShaderTable"));

	for (uint32 i = 0; i < maxRecords; ++i)
	{
		RootArguments rootArguments{
			.pushConstants = ClosestHitPushConstants{ .objectID = i }
		};

		hitGroupShaderTable[resourceIndex]->uploadRecord(i, INDIRECT_SPECULAR_HIT_GROUP_NAME, &rootArguments, sizeof(rootArguments));
	}

	CYLOG(LogIndirectSpecular, Log, L"Resize hit group shader table [%u]: %u records", resourceIndex, maxRecords);
}

void IndirecSpecularPass::prepareRaytracingResources(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput)
{
	const uint32 resourceIndex = passFrameInfo.currFrame;

	resizeTextures(commandList, passInput.unscaledRenderWidth, passInput.unscaledRenderHeight);

	// Resize hit group shader table if needed.
	// #todo-lod: Raytracing does not support LOD...
	uint32 requiredRecordCount = passInput.scene->totalMeshSectionsLOD0;
	if (requiredRecordCount > totalHitGroupShaderRecord[resourceIndex])
	{
		resizeHitGroupShaderTable(passFrameInfo, requiredRecordCount);
	}

	DispatchRaysDesc dispatchDesc{
		.raygenShaderTable = raygenShaderTable.get(),
		.missShaderTable   = missShaderTable.get(),
		.hitGroupTable     = hitGroupShaderTable.at(resourceIndex),
		.width             = passInput.sceneWidth,
		.height            = passInput.sceneHeight,
		.depth             = 1,
	};
	rayCommandGenerator->beginCommand(0);
	rayCommandGenerator->writeDispatchRaysArguments(dispatchDesc);
	rayCommandGenerator->endCommand();
	rayCommandGenerator->copyToBuffer(commandList, 1, rayCommandBuffer.get(), 0);
}

void IndirecSpecularPass::classifierPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput)
{
	const uint32 resourceIndex = passFrameInfo.currFrame;

	{
		SCOPED_DRAW_EVENT(commandList, TileClassification);

		const uint32 packedSize = Cymath::packUint16x2(passInput.sceneWidth, passInput.sceneHeight);

		uint32 zeroValue = 0;
		passInput.tileCounterBuffer->singleWriteToGPU(commandList, &zeroValue, sizeof(zeroValue), 0);

		{
			BufferBarrierAuto bufferBarriers[] = {
				{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, passInput.tileCoordBuffer },
				{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, passInput.tileCounterBuffer },
			};
			TextureBarrierAuto textureBarriers[] = {
				{
					EBarrierSync::DEPTH_STENCIL, EBarrierAccess::DEPTH_STENCIL_READ, EBarrierLayout::DepthStencilRead,
					passInput.sceneDepthTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
				}
			};
			commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);
		}

		ShaderParameterTable SPT{};
		SPT.pushConstants("pushConstants", { packedSize }, 0);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.rwBuffer("rwTileCoordBuffer", passInput.tileCoordBufferUAV);
		SPT.rwBuffer("rwTileCounterBuffer", passInput.tileCounterBufferUAV);

		uint32 requiredVolatiles = SPT.totalDescriptors();
		DescriptorHeap* volatileHeap = classifierPassDescriptor.resizeDescriptorHeap(resourceIndex, requiredVolatiles);

		commandList->setComputePipelineState(classifierPipeline.get());
		commandList->bindComputeShaderParameters(classifierPipeline.get(), &SPT, volatileHeap);

		uint32 dispatchX = (passInput.sceneWidth + 7) / 8;
		uint32 dispatchY = (passInput.sceneHeight + 7) / 8;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		GlobalBarrier globalBarrier = {
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING,
			EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS,
		};
		commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
	}
	{
		SCOPED_DRAW_EVENT(commandList, PrepareIndirectRays);

		BufferBarrierAuto bufferBarriers[] = {
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, rayCommandBuffer.get() },
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, amdCommandBuffer.get() },
		};
		commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, 0, nullptr, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.rwBuffer("rwTileCounterBuffer", passInput.tileCounterBufferUAV);
		SPT.rwBuffer("rwIndirectArgumentBuffer", rayCommandBufferUAV.get());
		SPT.rwBuffer("rwAmdReprojArgumentBuffer", amdCommandBufferUAV.get());

		uint32 requiredVolatiles = SPT.totalDescriptors();
		DescriptorHeap* volatileHeap = indirectRaysPassDescriptor.resizeDescriptorHeap(resourceIndex, requiredVolatiles);

		commandList->setComputePipelineState(indirectRaysPipeline.get());
		commandList->bindComputeShaderParameters(indirectRaysPipeline.get(), &SPT, volatileHeap);

		commandList->dispatchCompute(1, 1, 1);

		GlobalBarrier globalBarrier = {
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING,
			EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS,
		};
		commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
	}
}

void IndirecSpecularPass::raytracingPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, SpecularRaytracing);

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
		uboData->traceMode = (uint32)passInput.mode;

		auto uniformCBV = rayPassDescriptor.getUniformCBV(currFrame);
		uniformCBV->writeToGPU(commandList, uboData, sizeof(RayPassUniform));

		delete uboData;
	}

	commandList->setRaytracingPipelineState(RTPSO.get());

	auto radianceTexture = amdRadianceHistory.getTexture(prevFrame);
	auto radianceUAV     = amdRadianceHistory.getUAV(prevFrame);
	auto varianceTexture = amdVarianceHistory.getTexture(prevFrame);
	auto varianceUAV     = amdVarianceHistory.getUAV(prevFrame);

	TextureBarrierAuto textureBarriers[] = {
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			radianceTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
		},
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			varianceTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
		},
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	// Bind global shader parameters.
	{
		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.constantBuffer("passUniform", rayPassDescriptor.getUniformCBV(currFrame));
		SPT.accelerationStructure("rtScene", passInput.raytracingScene->getSRV());
		SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
		SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
		SPT.structuredBuffer("gpuSceneBuffer", passInput.gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("skybox", passInput.skyboxSRV);
		SPT.texture("gbuffer0", passInput.gbuffer0SRV);
		SPT.texture("gbuffer1", passInput.gbuffer1SRV);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.rwTexture("rwRaytracingTexture", raytracingUAV.get());
		SPT.rwTexture("rwRadianceTexture", radianceUAV);
		SPT.rwTexture("rwVarianceTexture", varianceUAV);
#if INDIRECT_DISPATCH_RAYS
		SPT.rwBuffer("rwTileCoordBuffer", passInput.tileCoordBufferUAV);
#endif
		// Bindless
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		// Resize volatile heaps if needed.
		uint32 requiredVolatiles = SPT.totalDescriptors();
		DescriptorHeap* volatileHeap = rayPassDescriptor.resizeDescriptorHeap(currFrame, requiredVolatiles);

		commandList->bindRaytracingShaderParameters(RTPSO.get(), &SPT, volatileHeap);
	}
	
#if INDIRECT_DISPATCH_RAYS
	// With indirect dispatch we don't write to all pixels anymore, so need to explicitly clear the texture.
	{
		SCOPED_DRAW_EVENT(commandList, ClearIndirectSpecularRaytracing);
		
		// #wip: Now I have clearResourcePass
		// #todo-rhi: ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat() requires TWO descriptor heaps for a single UAV?
		// Well let's just adopt the most lazy way...

		TextureBarrierAuto barriersBefore = TextureBarrierAuto::toRenderTarget(raytracingTexture.get());
		commandList->barrierAuto(0, nullptr, 1, &barriersBefore, 0, nullptr);

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(raytracingRTV.get(), clearColor);

		TextureBarrierAuto barriersAfter = {
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			raytracingTexture.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
		};
		commandList->barrierAuto(0, nullptr, 1, &barriersAfter, 0, nullptr);
	}

	commandList->executeIndirect(rayCommandSignature.get(), 1, rayCommandBuffer.get(), 0);

	{
		GlobalBarrier globalBarrier = {
			EBarrierSync::EXECUTE_INDIRECT, EBarrierSync::COMPUTE_SHADING,
			EBarrierAccess::INDIRECT_ARGUMENT, EBarrierAccess::UNORDERED_ACCESS
		};
		commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
	}
#else
	DispatchRaysDesc dispatchDesc{
		.raygenShaderTable = raygenShaderTable.get(),
		.missShaderTable   = missShaderTable.get(),
		.hitGroupTable     = hitGroupShaderTable.at(currFrame),
		.width             = sceneWidth,
		.height            = sceneHeight,
		.depth             = 1,
	};
	commandList->dispatchRays(dispatchDesc);
#endif
}

void IndirecSpecularPass::legacyDenoisingPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, TemporalReprojection);

	const uint32 currFrame = passFrameInfo.currFrame;
	const uint32 prevFrame = passFrameInfo.prevFrame;

	auto currColorTexture = colorHistory.getTexture(currFrame);
	auto prevColorTexture = colorHistory.getTexture(prevFrame);
	auto currColorUAV = colorHistory.getUAV(currFrame);
	auto prevColorSRV = colorHistory.getSRV(prevFrame);

	auto currMomentTexture = momentHistory.getTexture(currFrame);
	auto prevMomentTexture = momentHistory.getTexture(prevFrame);
	auto currMomentUAV = momentHistory.getUAV(currFrame);
	auto prevMomentSRV = momentHistory.getSRV(prevFrame);

	auto currSampleCountTexture = sampleCountHistory.getTexture(currFrame);
	auto prevSampleCountTexture = sampleCountHistory.getTexture(prevFrame);
	auto currSampleCountUAV = sampleCountHistory.getUAV(currFrame);
	auto prevSampleCountSRV = sampleCountHistory.getSRV(prevFrame);

	{
		TextureBarrierAuto textureBarriers[] = {
			TextureBarrierAuto::toUnorderedAccess(currColorTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(currMomentTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(currSampleCountTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(prevColorTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(prevMomentTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(prevSampleCountTexture, EBarrierSync::COMPUTE_SHADING),
		};
		commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);
	}

	// Update uniforms.
	{
		TemporalPassUniform uboData;

		uboData.screenSize[0] = actualHistoryWidth[currFrame];
		uboData.screenSize[1] = actualHistoryHeight[currFrame];
		uboData.prevScreenSize[0] = actualHistoryWidth[prevFrame];
		uboData.prevScreenSize[1] = actualHistoryHeight[prevFrame];
		uboData.prevInvScreenSize[0] = 1.0f / (float)actualHistoryWidth[prevFrame];
		uboData.prevInvScreenSize[1] = 1.0f / (float)actualHistoryHeight[prevFrame];
		uboData.bInvalidateHistory = (passInput.mode == EIndirectSpecularMode::ForceMirror);
		uboData.bLimitHistory = (passInput.mode == EIndirectSpecularMode::BRDF);

		auto uniformCBV = temporalPassDescriptor.getUniformCBV(currFrame);
		uniformCBV->writeToGPU(commandList, &uboData, sizeof(TemporalPassUniform));
	}

	// Bind shader parameters.
	{
		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.constantBuffer("passUniform", temporalPassDescriptor.getUniformCBV(currFrame));
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.texture("raytracingTexture", raytracingSRV.get());
		SPT.texture("prevSceneDepthTexture", passInput.prevSceneDepthSRV);
		SPT.texture("prevColorTexture", prevColorSRV);
		SPT.texture("prevMomentTexture", prevMomentSRV);
		SPT.texture("prevSampleCountTexture", prevSampleCountSRV);
		SPT.texture("velocityMapTexture", passInput.velocityMapSRV);
		SPT.rwTexture("currentColorTexture", currColorUAV);
		SPT.rwTexture("currentMomentTexture", currMomentUAV);
		SPT.rwTexture("currentSampleCountTexture", currSampleCountUAV);

		// Resize volatile heaps if needed.
		uint32 requiredVolatiles = SPT.totalDescriptors();
		DescriptorHeap* volatileHeap = temporalPassDescriptor.resizeDescriptorHeap(currFrame, requiredVolatiles);

		commandList->setComputePipelineState(temporalPipeline.get());
		commandList->bindComputeShaderParameters(temporalPipeline.get(), &SPT, volatileHeap);
	}

	// Dispatch compute and issue memory barriers.
	{
		uint32 dispatchX = (passInput.sceneWidth + 7) / 8;
		uint32 dispatchY = (passInput.sceneHeight + 7) / 8;
		commandList->dispatchCompute(dispatchX, dispatchY, 1);

		GlobalBarrier globalBarrier = {
			EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING,
			EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS,
		};
		commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
	}

	// -------------------------------------------------------------------
	// Phase: Spatial Reconstruction
	// #todo-specular: Spatial filter
}

void IndirecSpecularPass::amdReprojPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, AMDReproject);

	const uint32 currFrame = passFrameInfo.currFrame;
	const uint32 prevFrame = passFrameInfo.prevFrame;

	auto currRadianceTexture = amdRadianceHistory.getTexture(currFrame);
	auto prevRadianceTexture = amdRadianceHistory.getTexture(prevFrame);
	auto currRadianceSRV = amdRadianceHistory.getSRV(currFrame);
	auto prevRadianceSRV = amdRadianceHistory.getSRV(prevFrame);

	auto currSampleCountTexture = amdSampleCountHistory.getTexture(currFrame);
	auto prevSampleCountTexture = amdSampleCountHistory.getTexture(prevFrame);
	auto currSampleCountSRV = amdSampleCountHistory.getSRV(currFrame);
	auto prevSampleCountUAV = amdSampleCountHistory.getUAV(prevFrame);

	auto currVarianceTexture = amdVarianceHistory.getTexture(currFrame);
	auto prevVarianceTexture = amdVarianceHistory.getTexture(prevFrame);
	auto currVarianceSRV = amdVarianceHistory.getSRV(currFrame);
	auto prevVarianceUAV = amdVarianceHistory.getUAV(prevFrame);

	auto passUniformCBV = amdReprojectPassDescriptor.getUniformCBV(currFrame);
	
	AMDDenoiserUniform uniformData{
		.invProjection           = passInput.invProjection.transpose(),
		.invView                 = passInput.invView.transpose(),
		.prevViewProjection      = passInput.prevViewProjection.transpose(),
		.renderSize              = { passInput.unscaledRenderWidth, passInput.unscaledRenderHeight },
		.invRenderSize           = { 1.0f / (float)passInput.unscaledRenderWidth, 1.0f / (float)passInput.unscaledRenderHeight },
		.motionVectorScale       = { 1.0f, 1.0f },
		.normalsUnpackMul        = 1.0f,
		.normalsUnpackAdd        = 0.0f,
		.isRoughnessPerceptual   = AMD_IS_ROUGHNESS_PERCEPTUAL,
		.temporalStabilityFactor = AMD_TEMPORAL_STABILITY_FACTOR,
		.roughnessThreshold      = AMD_ROUGHNESS_THRESHOLD,
	};
	passUniformCBV->writeToGPU(commandList, &uniformData, sizeof(uniformData));

	BufferBarrierAuto bufferBarriers[] = {
		{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, passInput.tileCoordBuffer },
	};
	TextureBarrierAuto textureBarriers[] = {
		TextureBarrierAuto::toShaderResource(passInput.hizTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.velocityMapTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.normalTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(currRadianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(prevRadianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(currVarianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(currSampleCountTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.roughnessTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.prevSceneDepthTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.prevNormalTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.prevRoughnessTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(prevVarianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(prevSampleCountTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(avgRadianceTexture.get(), EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(reprojectedRadianceTexture.get(), EBarrierSync::COMPUTE_SHADING),
	};
	commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);

	// See ffx_denoiser_reproject_reflections_pass.hlsl
	ShaderResourceView* hizSRV                  = passInput.hizSRV; // tex2d, r32
	ShaderResourceView* motionVectorSRV         = passInput.velocityMapSRV; // tex2d, rg32
	ShaderResourceView* normalSRV               = passInput.normalSRV; // tex2d, rgb32 (world space)
	ShaderResourceView* radianceSRV             = prevRadianceSRV; // tex2d, rgba32 (w = ray length)
	ShaderResourceView* radianceHistorySRV      = currRadianceSRV; // tex2d, rgba32 (w not used)
	ShaderResourceView* varianceSRV             = currVarianceSRV; // tex2d, r32 (history; for prev frame)
	ShaderResourceView* sampleCountSRV          = currSampleCountSRV; // tex2d, r32 (history; for prev frame)
	ShaderResourceView* extractedRoughnessSRV   = passInput.roughnessSRV; // tex2d, r32 (not perceptual. see ffx_denoiser_reflections_callbacks_hlsl.h)
	ShaderResourceView* depthHistorySRV         = passInput.prevSceneDepthSRV; // tex2d, r32
	ShaderResourceView* normalHistorySRV        = passInput.prevNormalSRV; // tex2d, rgb32 (world space)
	ShaderResourceView* roughnessHistorySRV     = passInput.prevRoughnessSRV; // tex2d, r32
	UnorderedAccessView* varianceUAV            = prevVarianceUAV; // rwTex2d, r32 (writeonly)
	UnorderedAccessView* sampleCountUAV         = prevSampleCountUAV; // rwTex2d, r32 (writeonly)
	UnorderedAccessView* averageRadianceUAV     = avgRadianceUAV.get(); // rwTex2d, rgb32 (writeonly, per 8x8 tile)
	UnorderedAccessView* denoiserTileListUAV    = passInput.tileCoordBufferUAV; // rwStructuredBuffer<uint> (readonly)
	UnorderedAccessView* reprojRadianceUAV      = reprojectedRadianceUAV.get(); // rwTex2d, rgb32 (writeonly, w not used)

	// Param names from ffx_denoiser_reflections_callbacks_hlsl.h
	ShaderParameterTable SPT{};
	SPT.constantBuffer("cbDenoiserReflections", passUniformCBV);
	SPT.texture("r_input_depth_hierarchy", hizSRV);
	SPT.texture("r_input_motion_vectors", motionVectorSRV);
	SPT.texture("r_input_normal", normalSRV);
	SPT.texture("r_radiance", radianceSRV);
	SPT.texture("r_radiance_history", radianceHistorySRV);
	SPT.texture("r_variance", varianceSRV);
	SPT.texture("r_sample_count", sampleCountSRV);
	SPT.texture("r_extracted_roughness", extractedRoughnessSRV);
	SPT.texture("r_depth_history", depthHistorySRV);
	SPT.texture("r_normal_history", normalHistorySRV);
	SPT.texture("r_roughness_history", roughnessHistorySRV);
	SPT.rwTexture("rw_variance", varianceUAV);
	SPT.rwTexture("rw_sample_count", sampleCountUAV);
	SPT.rwTexture("rw_average_radiance", averageRadianceUAV);
	SPT.rwStructuredBuffer("rw_denoiser_tile_list", denoiserTileListUAV);
	SPT.rwTexture("rw_reprojected_radiance", reprojRadianceUAV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	DescriptorHeap* volatileHeap = amdReprojectPassDescriptor.resizeDescriptorHeap(currFrame, requiredVolatiles);

	commandList->setComputePipelineState(amdReprojectPipeline.get());
	commandList->bindComputeShaderParameters(amdReprojectPipeline.get(), &SPT, volatileHeap);

	// Dispatch compute and issue memory barriers.
	commandList->executeIndirect(amdCommandSignature.get(), 1, amdCommandBuffer.get(), 0);

	GlobalBarrier globalBarrier = {
		EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING,
		EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS,
	};
	commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
}

void IndirecSpecularPass::amdPrefilterPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, AMDPrefilter);

	const uint32 currFrame = passFrameInfo.currFrame;
	const uint32 prevFrame = passFrameInfo.prevFrame;

	auto currRadianceTexture = amdRadianceHistory.getTexture(currFrame);
	auto prevRadianceTexture = amdRadianceHistory.getTexture(prevFrame);
	auto currRadianceUAV = amdRadianceHistory.getUAV(currFrame);
	auto prevRadianceSRV = amdRadianceHistory.getSRV(prevFrame);

	auto currVarianceTexture = amdVarianceHistory.getTexture(currFrame);
	auto prevVarianceTexture = amdVarianceHistory.getTexture(prevFrame);
	auto currVarianceUAV = amdVarianceHistory.getUAV(currFrame);
	auto prevVarianceSRV = amdVarianceHistory.getSRV(prevFrame);

	// Reuse CBV from reproj phase.
	auto passUniformCBV = amdReprojectPassDescriptor.getUniformCBV(currFrame);

	BufferBarrierAuto bufferBarriers[] = {
		{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, passInput.tileCoordBuffer },
	};
	TextureBarrierAuto textureBarriers[] = {
		TextureBarrierAuto::toShaderResource(passInput.hizTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.normalTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(prevRadianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(prevVarianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(avgRadianceTexture.get(), EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.roughnessTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(currRadianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(currVarianceTexture, EBarrierSync::COMPUTE_SHADING),
	};
	commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);

	// See ffx_denoiser_prefilter_reflections_pass.hlsl
	ShaderResourceView* hizSRV                  = passInput.hizSRV; // tex2d, r32
	ShaderResourceView* normalSRV               = passInput.normalSRV; // tex2d, rgb32 (world space)
	ShaderResourceView* radianceSRV             = prevRadianceSRV; // tex2d, rgba32 (w = ray length)
	ShaderResourceView* varianceSRV             = prevVarianceSRV; // tex2d, r32
	ShaderResourceView* avgRadianceSRV          = this->avgRadianceSRV.get(); // tex2d, r32
	ShaderResourceView* extractedRoughnessSRV   = passInput.roughnessSRV; // tex2d, r32 (not perceptual. see ffx_denoiser_reflections_callbacks_hlsl.h)
	UnorderedAccessView* radianceUAV            = currRadianceUAV; // rwTex2d, rgb32 (writeonly, w not used)
	UnorderedAccessView* varianceUAV            = currVarianceUAV; // rwTex2d, r32 (writeonly)
	UnorderedAccessView* denoiserTileListUAV    = passInput.tileCoordBufferUAV; // rwStructuredBuffer<uint> (readonly)

	// Param names from ffx_denoiser_reflections_callbacks_hlsl.h
	ShaderParameterTable SPT{};
	SPT.constantBuffer("cbDenoiserReflections", passUniformCBV);
	SPT.texture("r_input_depth_hierarchy", hizSRV);
	SPT.texture("r_input_normal", normalSRV);
	SPT.texture("r_radiance", radianceSRV);
	SPT.texture("r_variance", varianceSRV);
	SPT.texture("r_average_radiance", avgRadianceSRV);
	SPT.texture("r_extracted_roughness", extractedRoughnessSRV);
	SPT.rwTexture("rw_radiance", radianceUAV);
	SPT.rwTexture("rw_variance", varianceUAV);
	SPT.rwStructuredBuffer("rw_denoiser_tile_list", denoiserTileListUAV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	DescriptorHeap* volatileHeap = amdPrefilterPassDescriptor.resizeDescriptorHeap(currFrame, requiredVolatiles);

	commandList->setComputePipelineState(amdPrefilterPipeline.get());
	commandList->bindComputeShaderParameters(amdPrefilterPipeline.get(), &SPT, volatileHeap);

	// Dispatch compute and issue memory barriers.
	commandList->executeIndirect(amdCommandSignature.get(), 1, amdCommandBuffer.get(), 0);

	GlobalBarrier globalBarrier = {
		EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING,
		EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS,
	};
	commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
}

void IndirecSpecularPass::amdResolveTemporalPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, AMDResolveTemporal);

	const uint32 currFrame = passFrameInfo.currFrame;
	const uint32 prevFrame = passFrameInfo.prevFrame;

	auto currRadianceTexture = amdRadianceHistory.getTexture(currFrame);
	auto prevRadianceTexture = amdRadianceHistory.getTexture(prevFrame);
	auto currRadianceSRV = amdRadianceHistory.getSRV(currFrame);
	auto prevRadianceUAV = amdRadianceHistory.getUAV(prevFrame);

	auto currVarianceTexture = amdVarianceHistory.getTexture(currFrame);
	auto prevVarianceTexture = amdVarianceHistory.getTexture(prevFrame);
	auto currVarianceSRV = amdVarianceHistory.getSRV(currFrame);
	auto prevVarianceUAV = amdVarianceHistory.getUAV(prevFrame);

	auto currSampleCountTexture = amdSampleCountHistory.getTexture(currFrame);
	auto currSampleCountSRV = amdSampleCountHistory.getSRV(currFrame);

	// Reuse CBV from reproj phase.
	auto passUniformCBV = amdReprojectPassDescriptor.getUniformCBV(currFrame);

	BufferBarrierAuto bufferBarriers[] = {
		{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, passInput.tileCoordBuffer },
	};
	TextureBarrierAuto textureBarriers[] = {
		TextureBarrierAuto::toShaderResource(currRadianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(currVarianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(currSampleCountTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(avgRadianceTexture.get(), EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.roughnessTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(reprojectedRadianceTexture.get(), EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(prevRadianceTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(prevVarianceTexture, EBarrierSync::COMPUTE_SHADING),
	};
	commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);

	// See ffx_denoiser_resolve_temporal_reflections_pass.hlsl
	ShaderResourceView* radianceSRV             = currRadianceSRV; // tex2d, rgba32 (w = ray length)
	ShaderResourceView* varianceSRV             = currVarianceSRV; // tex2d, r32 (history; for prev frame)
	ShaderResourceView* sampleCountSRV          = currSampleCountSRV; // tex2d, r32 (history; for prev frame)
	ShaderResourceView* avgRadianceSRV          = this->avgRadianceSRV.get(); // tex2d, r32
	ShaderResourceView* extractedRoughnessSRV   = passInput.roughnessSRV; // tex2d, r32 (not perceptual. see ffx_denoiser_reflections_callbacks_hlsl.h)
	ShaderResourceView* reprojRadianceSRV       = reprojectedRadianceSRV.get();
	UnorderedAccessView* radianceUAV            = prevRadianceUAV; // rwTex2d, rgb32 (writeonly, w not used)
	UnorderedAccessView* varianceUAV            = prevVarianceUAV; // rwTex2d, r32 (writeonly)
	UnorderedAccessView* denoiserTileListUAV    = passInput.tileCoordBufferUAV; // rwStructuredBuffer<uint> (readonly)

	// Param names from ffx_denoiser_reflections_callbacks_hlsl.h
	ShaderParameterTable SPT{};
	SPT.constantBuffer("cbDenoiserReflections", passUniformCBV);
	SPT.texture("r_radiance", radianceSRV);
	SPT.texture("r_variance", varianceSRV);
	SPT.texture("r_sample_count", sampleCountSRV);
	SPT.texture("r_average_radiance", avgRadianceSRV);
	SPT.texture("r_extracted_roughness", extractedRoughnessSRV);
	SPT.texture("r_reprojected_radiance", reprojRadianceSRV);
	SPT.rwTexture("rw_radiance", radianceUAV);
	SPT.rwTexture("rw_variance", varianceUAV);
	SPT.rwStructuredBuffer("rw_denoiser_tile_list", denoiserTileListUAV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	DescriptorHeap* volatileHeap = amdResolveTemporalPassDescriptor.resizeDescriptorHeap(currFrame, requiredVolatiles);

	commandList->setComputePipelineState(amdResolveTemporalPipeline.get());
	commandList->bindComputeShaderParameters(amdResolveTemporalPipeline.get(), &SPT, volatileHeap);

	// Dispatch compute and issue memory barriers.
	commandList->executeIndirect(amdCommandSignature.get(), 1, amdCommandBuffer.get(), 0);

	GlobalBarrier globalBarrier = {
		EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING,
		EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS,
	};
	commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
}

void IndirecSpecularPass::amdFinalizeOutputPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, AMDFinalizeOutput);

	const uint32 currFrame = passFrameInfo.currFrame;
	const uint32 prevFrame = passFrameInfo.prevFrame;

	auto prevRadianceTexture = amdRadianceHistory.getTexture(prevFrame);
	auto prevRadianceSRV = amdRadianceHistory.getSRV(prevFrame);

	{
		// #todo-rhi: ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat() requires TWO descriptor heaps for a single UAV?
		// Well let's just adopt the most lazy way...

		TextureBarrierAuto textureBarrier = TextureBarrierAuto::toRenderTarget(amdFinalColorTexture.get());
		commandList->barrierAuto(0, nullptr, 1, &textureBarrier, 0, nullptr);

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(amdFinalColorRTV.get(), clearColor);
	}
	{
		BufferBarrierAuto bufferBarriers[] = {
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, passInput.tileCoordBuffer },
		};
		TextureBarrierAuto textureBarriers[] = {
			TextureBarrierAuto::toShaderResource(prevRadianceTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(amdFinalColorTexture.get(), EBarrierSync::COMPUTE_SHADING),
		};
		commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.pushConstants("pushConstants", { passInput.sceneWidth, passInput.sceneHeight });
		SPT.texture("inRadiance", prevRadianceSRV); // resolve temporal pass ended up writing the result to prev radiance...
		SPT.rwBuffer("rwTileCoordBuffer", passInput.tileCoordBufferUAV);
		SPT.rwTexture("rwRadiance", amdFinalColorUAV.get());

		uint32 requiredVolatiles = SPT.totalDescriptors();
		DescriptorHeap* volatileHeap = amdFinalizePassDescriptor.resizeDescriptorHeap(currFrame, requiredVolatiles);

		commandList->setComputePipelineState(amdFinalizePipeline.get());
		commandList->bindComputeShaderParameters(amdFinalizePipeline.get(), &SPT, volatileHeap);

		// Dispatch compute and issue memory barriers.
		commandList->executeIndirect(amdCommandSignature.get(), 1, amdCommandBuffer.get(), 0);
	}
	{
		GlobalBarrier globalBarrier = {
			EBarrierSync::EXECUTE_INDIRECT, EBarrierSync::COMPUTE_SHADING,
			EBarrierAccess::INDIRECT_ARGUMENT, EBarrierAccess::UNORDERED_ACCESS
		};
		commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
	}
}
