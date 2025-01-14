#include "path_tracing_pass.h"
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

// I don't call TraceRays() recursively, so this constant actually doesn't matter.
// Rather see MAX_BOUNCE in path_tracing.hlsl.
#define PATH_TRACING_MAX_RECURSION            2
#define PATH_TRACING_HIT_GROUP_NAME           L"PathTracing_HitGroup"

#define SHADER_SOURCE_FILE                    L"path_tracing.hlsl"
#define MAIN_RAYGEN                           "MainRaygen"
#define MAIN_CLOSEST_HIT                      "MainClosestHit"
#define MAIN_MISS                             "MainMiss"

#define RANDOM_SEQUENCE_LENGTH                (64 * 64)

DEFINE_LOG_CATEGORY_STATIC(LogPathTracing);

struct PathTracingUniform
{
	float randFloats0[RANDOM_SEQUENCE_LENGTH];
	float randFloats1[RANDOM_SEQUENCE_LENGTH];
	Float4x4 prevViewInv;
	Float4x4 prevProjInv;
	Float4x4 prevViewProj;
	uint32 renderTargetWidth;
	uint32 renderTargetHeight;
	uint32 bInvalidateHistory;
	uint32 bLimitHistory;
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

// Just to calculate size in bytes.
// Should match with RayPayload in path_tracing.hlsl.
struct RayPayload
{
	float  surfaceNormal[3];
	float  roughness;
	float  albedo[3];
	float  hitTime;
	float  emission[3];
	uint32 objectID;
};
// Just to calculate size in bytes.
// Should match with IntersectionAttributes in path_tracing.hlsl.
struct TriangleIntersectionAttributes
{
	float texcoord[2];
};

struct ClosestHitPushConstants
{
	uint32 objectID; // item index in gpu scene buffer
};
static_assert(sizeof(ClosestHitPushConstants) % 4 == 0);

void PathTracingPass::initialize()
{
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Path Tracing will be disabled.");
		return;
	}

	RenderDevice* device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	rayPassDescriptor.initialize(L"PathTracing_RayPass", swapchainCount, sizeof(PathTracingUniform));
	blurPassDescriptor.initialize(L"PathTracing_BlurPass", swapchainCount, sizeof(BlurUniform));

	totalHitGroupShaderRecord.resize(swapchainCount, 0);
	hitGroupShaderTable.initialize(swapchainCount);

	// Raytracing pipeline
	{
		// Shaders
		ShaderStage* raygenShader = device->createShader(EShaderStage::RT_RAYGEN_SHADER, "PathTracing_Raygen");
		ShaderStage* closestHitShader = device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "PathTracing_ClosestHit");
		ShaderStage* missShader = device->createShader(EShaderStage::RT_MISS_SHADER, "PathTracing_Miss");
		raygenShader->declarePushConstants();
		closestHitShader->declarePushConstants({ "g_closestHitCB" });
		missShader->declarePushConstants();
		raygenShader->loadFromFile(SHADER_SOURCE_FILE, MAIN_RAYGEN);
		closestHitShader->loadFromFile(SHADER_SOURCE_FILE, MAIN_CLOSEST_HIT);
		missShader->loadFromFile(SHADER_SOURCE_FILE, MAIN_MISS);

		// RTPSO
		RaytracingPipelineStateObjectDesc pipelineDesc{
			.hitGroupName                 = PATH_TRACING_HIT_GROUP_NAME,
			.hitGroupType                 = ERaytracingHitGroupType::Triangles,
			.raygenShader                 = raygenShader,
			.closestHitShader             = closestHitShader,
			.missShader                   = missShader,
			.raygenLocalParameters        = {},
			.closestHitLocalParameters    = { "g_closestHitCB" },
			.missLocalParameters          = {},
			.maxPayloadSizeInBytes        = sizeof(RayPayload),
			.maxAttributeSizeInBytes      = sizeof(TriangleIntersectionAttributes),
			.maxTraceRecursionDepth       = PATH_TRACING_MAX_RECURSION,
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

bool PathTracingPass::isAvailable() const
{
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void PathTracingPass::renderPathTracing(RenderCommandList* commandList, uint32 swapchainIndex, const PathTracingInput& passInput)
{
	auto scene              = passInput.scene;
	auto camera             = passInput.camera;

	auto bCameraHasMoved    = passInput.bCameraHasMoved;
	auto sceneWidth         = passInput.sceneWidth;
	auto sceneHeight        = passInput.sceneHeight;
	auto gpuScene           = passInput.gpuScene;
	auto raytracingScene    = passInput.raytracingScene;
	auto sceneUniformBuffer = passInput.sceneUniformBuffer;
	auto sceneColorUAV      = passInput.sceneColorUAV;
	auto skyboxSRV          = passInput.skyboxSRV;

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

	auto currentColorUAV = colorHistoryUAV[swapchainIndex % 2].get();
	auto prevColorUAV = colorHistoryUAV[(swapchainIndex + 1) % 2].get();

	// Update uniforms.
	{
		PathTracingUniform* uboData = new PathTracingUniform;

		for (uint32 i = 0; i < RANDOM_SEQUENCE_LENGTH; ++i)
		{
			uboData->randFloats0[i] = Cymath::randFloat();
			uboData->randFloats1[i] = Cymath::randFloat();
		}
		uboData->prevViewInv = passInput.prevViewInvMatrix;
		uboData->prevProjInv = passInput.prevProjInvMatrix;
		uboData->prevViewProj = passInput.prevViewProjMatrix;
		uboData->renderTargetWidth = sceneWidth;
		uboData->renderTargetHeight = sceneHeight;
		uboData->bInvalidateHistory = bCameraHasMoved && passInput.mode == EPathTracingMode::Offline;
		uboData->bLimitHistory = passInput.mode == EPathTracingMode::Realtime;

		auto uniformCBV = rayPassDescriptor.getUniformCBV(swapchainIndex);
		uniformCBV->writeToGPU(commandList, uboData, sizeof(PathTracingUniform));

		delete uboData;
	}

	// -------------------------------------------------------------------
	// Phase: Raytracing + Temporal Reconstruction

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // rtScene
		requiredVolatiles += 1; // gIndexBuffer
		requiredVolatiles += 1; // gVertexBuffer
		requiredVolatiles += 1; // gpuSceneBuffer
		requiredVolatiles += 1; // skybox
		requiredVolatiles += 1; // sceneDepthTexture
		requiredVolatiles += 1; // prevSceneDepthTexture
		requiredVolatiles += 1; // sceneNormalTexture
		requiredVolatiles += 1; // currentColorTexture
		requiredVolatiles += 1; // prevColorTexture
		requiredVolatiles += 1; // currentMoment
		requiredVolatiles += 1; // prevMoment
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // pathTracingUniform
		requiredVolatiles += 1; // gpuSceneDesc.constantsBufferSRV
		requiredVolatiles += gpuSceneDesc.srvCount; // albedoTextures[]

		rayPassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	}

	// Resize hit group shader table if needed.
	if (scene->bRebuildGPUScene || hitGroupShaderTable[swapchainIndex] == nullptr)
	{
		resizeHitGroupShaderTable(swapchainIndex, scene);
	}

	commandList->setRaytracingPipelineState(RTPSO.get());

	// Bind global shader parameters.
	{
		DescriptorHeap* volatileHeap = rayPassDescriptor.getDescriptorHeap(swapchainIndex);
		ConstantBufferView* uniformCBV = rayPassDescriptor.getUniformCBV(swapchainIndex);
		auto currentMomentUAV = momentHistoryUAV[swapchainIndex % 2].get();
		auto prevMomentUAV = momentHistoryUAV[(swapchainIndex + 1) % 2].get();

		ShaderParameterTable SPT{};
		SPT.accelerationStructure("rtScene", raytracingScene->getSRV());
		SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
		SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("skybox", skyboxSRV);
		SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
		SPT.texture("prevSceneDepthTexture", passInput.prevSceneDepthSRV);
		SPT.texture("sceneNormalTexture", passInput.gbuffer1SRV);
		SPT.rwTexture("currentColorTexture", currentColorUAV);
		SPT.rwTexture("prevColorTexture", prevColorUAV);
		SPT.rwTexture("currentMoment", currentMomentUAV);
		SPT.rwTexture("prevMoment", prevMomentUAV);
		SPT.constantBuffer("sceneUniform", sceneUniformBuffer);
		SPT.constantBuffer("pathTracingUniform", uniformCBV);
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

	const int32 BLUR_COUNT = 3;
	
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
		uboData.cPhi = 0.2f;
		uboData.nPhi = 1.0f;
		uboData.pPhi = 0.5f;
		uboData.textureWidth = passInput.sceneWidth;
		uboData.textureHeight = passInput.sceneHeight;
		uboData.bSkipBlur = (passInput.mode == EPathTracingMode::Offline) ? 1 : 0;

		auto uniformCBV = blurPassDescriptor.getUniformCBV(swapchainIndex);
		uniformCBV->writeToGPU(commandList, &uboData, sizeof(BlurUniform));
	}

	commandList->setComputePipelineState(blurPipelineState.get());

	// Bind shader parameters.
	DescriptorHeap* volatileHeap = blurPassDescriptor.getDescriptorHeap(swapchainIndex);
	ConstantBufferView* uniformCBV = blurPassDescriptor.getUniformCBV(swapchainIndex);
	DescriptorIndexTracker tracker;
	UnorderedAccessView* blurInput = prevColorUAV;
	UnorderedAccessView* blurOutput = colorScratchUAV.get();
	
	int32 actualBlurCount = (passInput.mode == EPathTracingMode::Offline) ? 1 : BLUR_COUNT;
	for (int32 phase = 0; phase < actualBlurCount; ++phase)
	{
		if (phase == actualBlurCount - 1) blurOutput = sceneColorUAV;

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

void PathTracingPass::resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight)
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
		EPixelFormat::R32G32B32A32_FLOAT, ETextureAccessFlags::UAV, historyWidth, historyHeight, 1, 1, 0);
	for (uint32 i = 0; i < 2; ++i)
	{
		std::wstring debugName = L"RT_PathTracingMomentHistory" + std::to_wstring(i);
		momentHistory[i] = UniquePtr<Texture>(gRenderDevice->createTexture(momentDesc));
		momentHistory[i]->setDebugName(debugName.c_str());

		momentHistoryUAV[i] = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(momentHistory[i].get(),
			UnorderedAccessViewDesc{
				.format         = momentDesc.format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{
					.mipSlice   = 0,
					.planeSlice = 0,
				},
			}
		));
	}

	TextureCreateParams colorDesc = TextureCreateParams::texture2D(
		EPixelFormat::R32G32B32A32_FLOAT, ETextureAccessFlags::UAV, historyWidth, historyHeight, 1, 1, 0);

	for (uint32 i = 0; i < 2; ++i)
	{
		std::wstring debugName = L"RT_PathTracingColorHistory" + std::to_wstring(i);
		colorHistory[i] = UniquePtr<Texture>(gRenderDevice->createTexture(colorDesc));
		colorHistory[i]->setDebugName(debugName.c_str());

		colorHistoryUAV[i] = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(colorHistory[i].get(),
			UnorderedAccessViewDesc{
				.format         = colorDesc.format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{
					.mipSlice   = 0,
					.planeSlice = 0,
				},
			}
		));
	}

	colorScratch = UniquePtr<Texture>(gRenderDevice->createTexture(colorDesc));
	colorScratch->setDebugName(L"RT_PathTracingColorScratch");

	colorScratchUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(colorScratch.get(),
		UnorderedAccessViewDesc{
			.format         = colorDesc.format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{
				.mipSlice   = 0,
				.planeSlice = 0,
			},
		}
	));
}

void PathTracingPass::resizeHitGroupShaderTable(uint32 swapchainIndex, const SceneProxy* scene)
{
	const uint32 totalRecords = scene->totalMeshSectionsLOD0;
	totalHitGroupShaderRecord[swapchainIndex] = totalRecords;

	struct RootArguments
	{
		ClosestHitPushConstants pushConstants;
	};

	hitGroupShaderTable[swapchainIndex] = UniquePtr<RaytracingShaderTable>(
		gRenderDevice->createRaytracingShaderTable(
			RTPSO.get(), totalRecords, sizeof(RootArguments), L"PathTracing_HitGroupShaderTable"));

	const uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();
	uint32 recordIx = 0;
	for (uint32 meshIx = 0; meshIx < numStaticMeshes; ++meshIx)
	{
		const uint32 numSections = (uint32)scene->staticMeshes[meshIx]->getSections(0).size();
		for (uint32 sectionIx = 0; sectionIx < numSections; ++sectionIx)
		{
			RootArguments rootArguments{
				.pushConstants = ClosestHitPushConstants{
					.objectID = recordIx
				}
			};

			hitGroupShaderTable[swapchainIndex]->uploadRecord(recordIx, PATH_TRACING_HIT_GROUP_NAME, &rootArguments, sizeof(rootArguments));
			++recordIx;
		}
	}

	CYLOG(LogPathTracing, Log, L"Resize hit group shader table [%u]: %u records", swapchainIndex, totalRecords);
}
