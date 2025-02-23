#include "indirect_diffuse_pass.h"

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

// I don't call TraceRays() recursively, so this constant actually doesn't matter.
// Rather see MAX_BOUNCE in indirect_diffuse_reflection.hlsl.
#define INDIRECT_DIFFUSE_MAX_RECURSION      1
#define INDIRECT_DIFFUSE_HIT_GROUP_NAME     L"IndirectDiffuse_HitGroup"

// #todo-indirect-diffuse: NVidia STBN
#define RANDOM_SEQUENCE_LENGTH              (64 * 64)

static const int32 BLUR_COUNT = 5;
static float const cPhi       = 1.0f;
static float const nPhi       = 1.0f;
static float const pPhi       = 1.0f;

DEFINE_LOG_CATEGORY_STATIC(LogIndirectDiffuse);

struct IndirectDiffuseUniform
{
	float       randFloats0[RANDOM_SEQUENCE_LENGTH];
	float       randFloats1[RANDOM_SEQUENCE_LENGTH];
	Float4x4    prevViewProjInv;
	Float4x4    prevViewProj;
	uint32      renderTargetWidth;
	uint32      renderTargetHeight;
};

struct BlurUniform
{
	float kernelAndOffset[4 * 25];
	float cPhi;
	float nPhi;
	float pPhi;
	float _pad0;
	uint32 textureWidth;
	uint32 textureHeight;
	uint32 bSkipBlur;
	uint32 _pad2;
};

// Should match with RayPayload in indirect_diffuse_reflection.hlsl.
struct RayPayload
{
	float  surfaceNormal[3];
	float  roughness;
	float  albedo[3];
	float  hitTime;
	float  emission[3];
	uint32 objectID;
};
// Should match with MyAttributes in indirect_diffuse_reflection.hlsl.
struct TriangleIntersectionAttributes
{
	float texcoord[2];
};

struct ClosestHitPushConstants
{
	uint32 objectID;
};
static_assert(sizeof(ClosestHitPushConstants) % 4 == 0);

void IndirectDiffusePass::initialize()
{
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Indirect Diffuse Reflection will be disabled.");
		return;
	}

	RenderDevice* device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	rayPassDescriptor.initialize(L"IndirectDiffuse_RayPass", swapchainCount, sizeof(IndirectDiffuseUniform));
	blurPassDescriptor.initialize(L"IndirectDiffuse_BlurPass", swapchainCount, sizeof(BlurUniform));

	totalHitGroupShaderRecord.resize(swapchainCount, 0);
	hitGroupShaderTable.initialize(swapchainCount);

	ShaderStage* raygenShader = device->createShader(EShaderStage::RT_RAYGEN_SHADER, "Diffuse_Raygen");
	ShaderStage* closestHitShader = device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "Diffuse_ClosestHit");
	ShaderStage* missShader = device->createShader(EShaderStage::RT_MISS_SHADER, "Diffuse_Miss");
	raygenShader->declarePushConstants();
	closestHitShader->declarePushConstants({ "g_closestHitCB" });
	missShader->declarePushConstants();
	raygenShader->loadFromFile(L"indirect_diffuse_reflection.hlsl", "MainRaygen");
	closestHitShader->loadFromFile(L"indirect_diffuse_reflection.hlsl", "MainClosestHit");
	missShader->loadFromFile(L"indirect_diffuse_reflection.hlsl", "MainMiss");

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
		StaticSamplerDesc{
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
		},
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

	// Blur pipeline
	{
		ShaderStage* shader = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "BilateralBlurCS");
		shader->declarePushConstants({ "pushConstants" });
		shader->loadFromFile(L"bilateral_blur.hlsl", "mainCS");

		blurPipelineState = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0 }
		));

		delete shader;
	}
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

	Texture* currentColorTexture = colorHistory[swapchainIndex % 2].get();
	Texture* prevColorTexture = colorHistory[(swapchainIndex + 1) % 2].get();

	auto currentColorUAV = colorHistoryUAV[swapchainIndex % 2].get();
	auto prevColorUAV = colorHistoryUAV[(swapchainIndex + 1) % 2].get();
	auto prevColorSRV = colorHistorySRV[(swapchainIndex + 1) % 2].get();

	{
		SCOPED_DRAW_EVENT(commandList, PrevColorBarrier);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, prevColorTexture },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);
	}

	// Update uniforms.
	{
		IndirectDiffuseUniform* uboData = new IndirectDiffuseUniform;

		for (uint32 i = 0; i < RANDOM_SEQUENCE_LENGTH; ++i)
		{
			uboData->randFloats0[i] = Cymath::randFloat();
			uboData->randFloats1[i] = Cymath::randFloat();
		}
		uboData->prevViewProjInv = passInput.prevViewProjInvMatrix;
		uboData->prevViewProj = passInput.prevViewProjMatrix;
		uboData->renderTargetWidth = sceneWidth;
		uboData->renderTargetHeight = sceneHeight;

		auto uniformCBV = rayPassDescriptor.getUniformCBV(swapchainIndex);
		uniformCBV->writeToGPU(commandList, uboData, sizeof(IndirectDiffuseUniform));

		delete uboData;
	}

	// -------------------------------------------------------------------
	// Phase: Raytracing + Temporal Reconstruction

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // indirectDiffuseUniform
		requiredVolatiles += 1; // gIndexBuffer
		requiredVolatiles += 1; // gVertexBuffer
		requiredVolatiles += 1; // gpuSceneBuffer
		requiredVolatiles += 1; // gpuSceneDesc.constantsBufferSRV
		requiredVolatiles += 1; // rtScene
		requiredVolatiles += 1; // skybox
		requiredVolatiles += 1; // sceneDepthTexture
		requiredVolatiles += 1; // prevSceneDepthTexture
		requiredVolatiles += 1; // renderTarget
		requiredVolatiles += 2; // gbuffer0, gbuffer1
		requiredVolatiles += 1; // currentColorTexture
		requiredVolatiles += 1; // prevColorTexture
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
		SPT.constantBuffer("indirectDiffuseUniform", uniformCBV);
		SPT.accelerationStructure("rtScene", raytracingScene->getSRV());
		SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
		SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("skybox", passInput.skyboxSRV);
		SPT.texture("gbuffer0", passInput.gbuffer0SRV);
		SPT.texture("gbuffer1", passInput.gbuffer1SRV);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.texture("prevSceneDepthTexture", passInput.prevSceneDepthSRV);
		SPT.texture("prevColorTexture", prevColorSRV);
		SPT.rwTexture("renderTarget", passInput.indirectDiffuseUAV);
		SPT.rwTexture("currentColorTexture", currentColorUAV);
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

	// -------------------------------------------------------------------
	// Phase: Spatial Reconstruction

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // pushConstants
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // blurUniform
		requiredVolatiles += 1; // inColorTexture
		requiredVolatiles += 1; // inNormalTexture
		requiredVolatiles += 1; // inDepthTexture
		requiredVolatiles += 1; // outputTexture

		blurPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles * BLUR_COUNT);
	}

	// Update uniforms.
	{
		BlurUniform uboData;
		int32 k = 0;
		float kernel1D[3] = { 1.0f, 2.0f / 3.0f, 1.0f / 6.0f };
		for (int32 y = -2; y <= 2; ++y)
		{
			for (int32 x = -2; x <= 2; ++x)
			{
				uboData.kernelAndOffset[k * 4 + 0] = kernel1D[std::abs(x)] * kernel1D[std::abs(y)];
				uboData.kernelAndOffset[k * 4 + 1] = (float)x;
				uboData.kernelAndOffset[k * 4 + 2] = (float)y;
				uboData.kernelAndOffset[k * 4 + 3] = 0.0f;
				++k;
			}
		}
		uboData.cPhi = cPhi;
		uboData.nPhi = nPhi;
		uboData.pPhi = pPhi;
		uboData.textureWidth = passInput.sceneWidth;
		uboData.textureHeight = passInput.sceneHeight;
		uboData.bSkipBlur = false;

		auto uniformCBV = blurPassDescriptor.getUniformCBV(swapchainIndex);
		uniformCBV->writeToGPU(commandList, &uboData, sizeof(BlurUniform));
	}

	{
		SCOPED_DRAW_EVENT(commandList, CopyColorToPrevColor);

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

	commandList->setComputePipelineState(blurPipelineState.get());

	// Bind shader parameters.
	DescriptorHeap* volatileHeap = blurPassDescriptor.getDescriptorHeap(swapchainIndex);
	ConstantBufferView* uniformCBV = blurPassDescriptor.getUniformCBV(swapchainIndex);
	DescriptorIndexTracker tracker;
	UnorderedAccessView* blurInput = prevColorUAV;
	UnorderedAccessView* blurOutput = colorScratchUAV.get();

	int32 actualBlurCount = BLUR_COUNT;
	for (int32 phase = 0; phase < actualBlurCount; ++phase)
	{
		if (phase == actualBlurCount - 1) blurOutput = passInput.indirectDiffuseUAV;

		ShaderParameterTable SPT{};
		SPT.pushConstant("pushConstants", phase + 1);
		SPT.constantBuffer("sceneUniform", sceneUniformBuffer);
		SPT.constantBuffer("blurUniform", uniformCBV);
		SPT.rwTexture("inColorTexture", blurInput);
		SPT.texture("inNormalTexture", passInput.gbuffer1SRV);
		SPT.texture("inDepthTexture", passInput.sceneDepthSRV);
		SPT.rwTexture("outputTexture", blurOutput);

		commandList->bindComputeShaderParameters(blurPipelineState.get(), &SPT, volatileHeap, &tracker);

		uint32 groupX = (sceneWidth + 7) / 8, groupY = (sceneHeight + 7) / 8;
		commandList->dispatchCompute(groupX, groupY, 1);

		auto temp = blurInput;
		blurInput = blurOutput;
		blurOutput = temp;
	}
}

void IndirectDiffusePass::resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight)
{
	if (historyWidth == newWidth && historyHeight == newHeight)
	{
		return;
	}
	historyWidth = newWidth;
	historyHeight = newHeight;

	commandList->enqueueDeferredDealloc(momentHistory[0].release(), true);
	commandList->enqueueDeferredDealloc(momentHistory[1].release(), true);
	commandList->enqueueDeferredDealloc(colorHistory[0].release(), true);
	commandList->enqueueDeferredDealloc(colorHistory[1].release(), true);
	commandList->enqueueDeferredDealloc(colorScratch.release(), true);

	TextureCreateParams momentDesc = TextureCreateParams::texture2D(
		EPixelFormat::R16G16B16A16_FLOAT, ETextureAccessFlags::UAV, historyWidth, historyHeight, 1, 1, 0);
	for (uint32 i = 0; i < 2; ++i)
	{
		std::wstring debugName = L"RT_DiffuseMomentHistory" + std::to_wstring(i);
		momentHistory[i] = UniquePtr<Texture>(gRenderDevice->createTexture(momentDesc));
		momentHistory[i]->setDebugName(debugName.c_str());

		momentHistoryUAV[i] = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(momentHistory[i].get(),
			UnorderedAccessViewDesc{
				.format         = momentDesc.format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			}
		));
	}

	TextureCreateParams colorDesc = TextureCreateParams::texture2D(
		EPixelFormat::R16G16B16A16_FLOAT, ETextureAccessFlags::UAV, historyWidth, historyHeight, 1, 1, 0);

	for (uint32 i = 0; i < 2; ++i)
	{
		std::wstring debugName = L"RT_DiffuseColorHistory" + std::to_wstring(i);
		colorHistory[i] = UniquePtr<Texture>(gRenderDevice->createTexture(colorDesc));
		colorHistory[i]->setDebugName(debugName.c_str());

		colorHistoryUAV[i] = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(colorHistory[i].get(),
			UnorderedAccessViewDesc{
				.format         = colorDesc.format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			}
		));
		colorHistorySRV[i] = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(colorHistory[i].get(),
			ShaderResourceViewDesc{
				.format              = colorDesc.format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = colorHistory[i]->getCreateParams().mipLevels,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
	}
	{
		SCOPED_DRAW_EVENT(commandList, ColorHistoryBarrier);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, colorHistory[0].get() },
			{ ETextureMemoryLayout::COMMON, ETextureMemoryLayout::UNORDERED_ACCESS, colorHistory[1].get() },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);
	}

	colorScratch = UniquePtr<Texture>(gRenderDevice->createTexture(colorDesc));
	colorScratch->setDebugName(L"RT_DiffuseColorScratch");

	colorScratchUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(colorScratch.get(),
		UnorderedAccessViewDesc{
			.format         = colorDesc.format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
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
