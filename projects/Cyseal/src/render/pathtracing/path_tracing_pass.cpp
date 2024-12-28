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
// Rather see MAX_BOUNCE in rt_reflection.hlsl.
#define PATH_TRACING_MAX_RECURSION            2
#define PATH_TRACING_HIT_GROUP_NAME           L"PathTracing_HitGroup"

#define SHADER_SOURCE_FILE                    L"path_tracing.hlsl"
#define MAIN_RAYGEN                           "MainRaygen"
#define MAIN_CLOSEST_HIT                      "MainClosestHit"
#define MAIN_MISS                             "MainMiss"

#define UNIFORM_MEMORY_POOL_SIZE              (256 * 1024) // 256 KiB
#define RANDOM_SEQUENCE_LENGTH                (64 * 64)

DEFINE_LOG_CATEGORY_STATIC(LogPathTracing);

struct PathTracingUniform
{
	float randFloats0[RANDOM_SEQUENCE_LENGTH];
	float randFloats1[RANDOM_SEQUENCE_LENGTH];
	uint32 renderTargetWidth;
	uint32 renderTargetHeight;
	uint32 bInvalidateHistory;
	uint32 _pad0;
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

	totalVolatileDescriptor.resize(swapchainCount, 0);
	volatileViewHeap.initialize(swapchainCount);

	totalHitGroupShaderRecord.resize(swapchainCount, 0);
	hitGroupShaderTable.initialize(swapchainCount);

	// Uniforms
	{
		CHECK(sizeof(PathTracingUniform) * swapchainCount <= UNIFORM_MEMORY_POOL_SIZE);

		uniformMemory = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = UNIFORM_MEMORY_POOL_SIZE,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC,
			}
		));

		uniformDescriptorHeap = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV,
				.numDescriptors = swapchainCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
			}
		));

		uint32 bufferOffset = 0;
		uniformCBVs.initialize(swapchainCount);
		for (uint32 i = 0; i < swapchainCount; ++i)
		{
			uniformCBVs[i] = UniquePtr<ConstantBufferView>(
				gRenderDevice->createCBV(
					uniformMemory.get(),
					uniformDescriptorHeap.get(),
					sizeof(PathTracingUniform),
					bufferOffset));

			uint32 alignment = gRenderDevice->getConstantBufferDataAlignment();
			bufferOffset += Cymath::alignBytes(sizeof(PathTracingUniform), alignment);
		}
	}

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

	// Acceleration Structure is built by SceneRenderer.
	// ...

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

bool PathTracingPass::isAvailable() const
{
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void PathTracingPass::renderPathTracing(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const SceneProxy* scene,
	const Camera* camera,
	bool bCameraHasMoved,
	ConstantBufferView* sceneUniformBuffer,
	AccelerationStructure* raytracingScene,
	GPUScene* gpuScene,
	UnorderedAccessView* sceneColorUAV,
	ShaderResourceView* skyboxSRV,
	uint32 sceneWidth,
	uint32 sceneHeight)
{
	if (isAvailable() == false)
	{
		return;
	}
	if (gpuScene->getGPUSceneItemMaxCount() == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}

	resizeTextures(commandList, sceneWidth, sceneHeight);

	// Update uniforms.
	{
		PathTracingUniform* uboData = new PathTracingUniform;

		for (uint32 i = 0; i < RANDOM_SEQUENCE_LENGTH; ++i)
		{
			uboData->randFloats0[i] = Cymath::randFloat();
			uboData->randFloats1[i] = Cymath::randFloat();
		}
		uboData->renderTargetWidth = sceneWidth;
		uboData->renderTargetHeight = sceneHeight;
		uboData->bInvalidateHistory = bCameraHasMoved;

		uniformCBVs[swapchainIndex]->writeToGPU(commandList, uboData, sizeof(PathTracingUniform));

		delete uboData;
	}

	// Resize volatile heaps if needed.
	{
		uint32 materialCBVCount, materialSRVCount;
		gpuScene->queryMaterialDescriptorsCount(swapchainIndex, materialCBVCount, materialSRVCount);

		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // rtScene
		requiredVolatiles += 1; // gIndexBuffer
		requiredVolatiles += 1; // gVertexBuffer
		requiredVolatiles += 1; // gpuSceneBuffer
		requiredVolatiles += 1; // skybox
		requiredVolatiles += 1; // renderTarget
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // pathTracingUniform
		requiredVolatiles += materialCBVCount;
		requiredVolatiles += materialSRVCount;

		if (requiredVolatiles > totalVolatileDescriptor[swapchainIndex])
		{
			resizeVolatileHeap(swapchainIndex, requiredVolatiles);
		}
	}

	// Resize hit group shader table if needed.
	if (scene->bRebuildGPUScene || hitGroupShaderTable[swapchainIndex] == nullptr)
	{
		resizeHitGroupShaderTable(swapchainIndex, scene);
	}

	commandList->setRaytracingPipelineState(RTPSO.get());

	// Bind global shader parameters.
	{
		DescriptorHeap* volatileHeap = volatileViewHeap.at(swapchainIndex);
		auto gpuSceneDesc = gpuScene->queryMaterialDescriptors(swapchainIndex);

		ShaderParameterTable SPT{};
		SPT.accelerationStructure("rtScene", raytracingScene->getSRV());
		SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
		SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.texture("skybox", skyboxSRV);
		SPT.rwTexture("renderTarget", sceneColorUAV);
		SPT.constantBuffer("sceneUniform", sceneUniformBuffer);
		SPT.constantBuffer("pathTracingUniform", uniformCBVs[swapchainIndex].get());
		// Bindless
		SPT.constantBuffer("materials", gpuSceneDesc.cbvHeap, 0, gpuSceneDesc.cbvCount);
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

	TextureCreateParams texDesc = TextureCreateParams::texture2D(
		EPixelFormat::R32G32B32A32_FLOAT,
		ETextureAccessFlags::UAV,
		historyWidth, historyHeight,
		1, 1, 0);

	momentHistory[0] = UniquePtr<Texture>(gRenderDevice->createTexture(texDesc));
	momentHistory[0]->setDebugName(L"RT_PathTracingMomentHistory0");
	momentHistory[1] = UniquePtr<Texture>(gRenderDevice->createTexture(texDesc));
	momentHistory[1]->setDebugName(L"RT_PathTracingMomentHistory1");
}

void PathTracingPass::resizeVolatileHeap(uint32 swapchainIndex, uint32 maxDescriptors)
{
	totalVolatileDescriptor[swapchainIndex] = maxDescriptors;

	volatileViewHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::CBV_SRV_UAV,
			.numDescriptors = maxDescriptors,
			.flags          = EDescriptorHeapFlags::ShaderVisible,
			.nodeMask       = 0,
		}
	));

	wchar_t debugName[256];
	swprintf_s(debugName, L"PathTracing_VolatileViewHeap_%u", swapchainIndex);
	volatileViewHeap[swapchainIndex]->setDebugName(debugName);

	CYLOG(LogPathTracing, Log, L"Resize volatile heap [%u]: %u descriptors", swapchainIndex, maxDescriptors);
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
