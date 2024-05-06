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

namespace RootParameters
{
	enum Value
	{
		OutputViewSlot = 0,
		AccelerationStructureSlot,
		SceneUniformSlot,
		PathTracingUniformSlot,
		GlobalIndexBufferSlot,
		GlobalVertexBufferSlot,
		GPUSceneSlot,
		SkyboxSlot,
		MaterialConstantsSlot,
		MaterialTexturesSlot,
		Count
	};
}

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

struct RayGenConstantBuffer
{
	Float4x4 dummyValue;
};
struct ClosestHitPushConstants
{
	uint32 objectID; // item index in gpu scene buffer
};
static_assert(sizeof(RayGenConstantBuffer) % 4 == 0);
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

		uniformMemory = std::unique_ptr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = UNIFORM_MEMORY_POOL_SIZE,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC,
			}
		));

		uniformDescriptorHeap = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(
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
			uniformCBVs[i] = std::unique_ptr<ConstantBufferView>(
				gRenderDevice->createCBV(
					uniformMemory.get(),
					uniformDescriptorHeap.get(),
					sizeof(PathTracingUniform),
					bufferOffset));

			uint32 alignment = gRenderDevice->getConstantBufferDataAlignment();
			bufferOffset += Cymath::alignBytes(sizeof(PathTracingUniform), alignment);
		}
	}

	// Global root signature
	{
		DescriptorRange descRanges[6];
		// renderTarget       = register(u0, space0)
		descRanges[0].init(EDescriptorRangeType::UAV, 1, 0, 0);
		// sceneUniform       = register(b0, space0)
		descRanges[1].init(EDescriptorRangeType::CBV, 1, 0, 0);
		// pathTracingUniform = register(b1, space0)
		descRanges[2].init(EDescriptorRangeType::CBV, 1, 1, 0);
		// skybox
		descRanges[3].init(EDescriptorRangeType::SRV, 1, 4, 0); // register(t4, space0)
		// material CBVs & SRVs (bindless)
		descRanges[4].init(EDescriptorRangeType::CBV, (uint32)(-1), 0, 3); // register(b0, space3)
		descRanges[5].init(EDescriptorRangeType::SRV, (uint32)(-1), 0, 3); // register(t0, space3)

		// https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits
		// Let's be careful of root signature limit as my parameters are growing a little bit...
		// max size         = 64 dwords
		// descriptor table = 1 dword
		// root constant    = 1 dword
		// root descriptor  = 2 dwords

		RootParameter rootParameters[RootParameters::Count];
		rootParameters[RootParameters::OutputViewSlot].initAsDescriptorTable(1, &descRanges[0]);
		rootParameters[RootParameters::AccelerationStructureSlot].initAsSRV(0, 0);                       // register(t0, space0)
		rootParameters[RootParameters::SceneUniformSlot].initAsDescriptorTable(1, &descRanges[1]);       // register(b0, space0)
		rootParameters[RootParameters::PathTracingUniformSlot].initAsDescriptorTable(1, &descRanges[2]); // register(b0, space0)
		rootParameters[RootParameters::GlobalIndexBufferSlot].initAsSRV(1, 0);                           // register(t1, space0)
		rootParameters[RootParameters::GlobalVertexBufferSlot].initAsSRV(2, 0);                          // register(t2, space0)
		rootParameters[RootParameters::GPUSceneSlot].initAsSRV(3, 0);                                    // register(t3, space0)
		rootParameters[RootParameters::SkyboxSlot].initAsDescriptorTable(1, &descRanges[3]);             // register(t4, space0)

		rootParameters[RootParameters::MaterialConstantsSlot].initAsDescriptorTable(1, &descRanges[4]);
		rootParameters[RootParameters::MaterialTexturesSlot].initAsDescriptorTable(1, &descRanges[5]);

		StaticSamplerDesc staticSamplers[] = {
			// Material albedo sampler
			{
				.filter = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT,
				.addressU = ETextureAddressMode::Wrap,
				.addressV = ETextureAddressMode::Wrap,
				.addressW = ETextureAddressMode::Wrap,
				.shaderRegister = 0,
				.shaderVisibility = EShaderVisibility::All,
			},
			// Skybox sampler
			{
				.filter = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT,
				.addressU = ETextureAddressMode::Wrap,
				.addressV = ETextureAddressMode::Wrap,
				.addressW = ETextureAddressMode::Wrap,
				.shaderRegister = 1,
				.shaderVisibility = EShaderVisibility::All,
			},
		};

		RootSignatureDesc sigDesc(
			_countof(rootParameters), rootParameters,
			_countof(staticSamplers), staticSamplers);
		globalRootSignature = UniquePtr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}

	// Local root signature
	{
		RootParameter rootParameters[1];
		rootParameters[0].initAsConstants(0, 1, sizeof(RayGenConstantBuffer) / 4); // register(b0, space1)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
		sigDesc.flags = ERootSignatureFlags::LocalRootSignature;
		raygenLocalRootSignature = UniquePtr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}
	{
		RootParameter rootParameters[1];
		rootParameters[0].initAsConstants(0, 2, sizeof(ClosestHitPushConstants) / 4); // register(b0, space2)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
		sigDesc.flags = ERootSignatureFlags::LocalRootSignature;
		closestHitLocalRootSignature = UniquePtr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}

	// RTPSO
	{
		raygenShader = UniquePtr<ShaderStage>(device->createShader(EShaderStage::RT_RAYGEN_SHADER, "PathTracing_Raygen"));
		closestHitShader = UniquePtr<ShaderStage>(device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "PathTracing_ClosestHit"));
		missShader = UniquePtr<ShaderStage>(device->createShader(EShaderStage::RT_MISS_SHADER, "PathTracing_Miss"));

		raygenShader->loadFromFile(SHADER_SOURCE_FILE, MAIN_RAYGEN);
		closestHitShader->loadFromFile(SHADER_SOURCE_FILE, MAIN_CLOSEST_HIT);
		missShader->loadFromFile(SHADER_SOURCE_FILE, MAIN_MISS);

		RaytracingPipelineStateObjectDesc desc;
		desc.hitGroupName                 = PATH_TRACING_HIT_GROUP_NAME;
		desc.hitGroupType                 = ERaytracingHitGroupType::Triangles;
		desc.raygenShader                 = raygenShader.get();
		desc.closestHitShader             = closestHitShader.get();
		desc.missShader                   = missShader.get();
		desc.raygenLocalRootSignature     = raygenLocalRootSignature.get();
		desc.closestHitLocalRootSignature = closestHitLocalRootSignature.get();
		desc.missLocalRootSignature       = nullptr;
		desc.globalRootSignature          = globalRootSignature.get();
		desc.maxPayloadSizeInBytes        = sizeof(RayPayload);
		desc.maxAttributeSizeInBytes      = sizeof(TriangleIntersectionAttributes);
		desc.maxTraceRecursionDepth       = PATH_TRACING_MAX_RECURSION;

		RTPSO = UniquePtr<RaytracingPipelineStateObject>(
			gRenderDevice->createRaytracingPipelineStateObject(desc));
	}

	// Acceleration Structure is built by SceneRenderer.
	// ...

	// Raygen shader table
	{
		struct RootArguments
		{
			RayGenConstantBuffer cb;
		} rootArguments;
		::memset(&(rootArguments.cb.dummyValue), 0, sizeof(rootArguments.cb.dummyValue));

		uint32 numShaderRecords = 1;
		raygenShaderTable = UniquePtr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(
				RTPSO.get(), numShaderRecords, sizeof(rootArguments), L"RayGenShaderTable"));
		raygenShaderTable->uploadRecord(0, raygenShader.get(), &rootArguments, sizeof(rootArguments));
	}
	// Miss shader table
	{
		uint32 numShaderRecords = 1;
		missShaderTable = UniquePtr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(
				RTPSO.get(), numShaderRecords, 0, L"MissShaderTable"));
		missShaderTable->uploadRecord(0, missShader.get(), nullptr, 0);
	}
	// Hit group shader table is created in resizeHitGroupShaderTable().
	// ...
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
	Texture* renderTargetTexture,
	uint32 sceneWidth,
	uint32 sceneHeight)
{
	if (isAvailable() == false)
	{
		return;
	}

	// Update uniforms.
	{
		PathTracingUniform* uboData = new PathTracingUniform;

		for (uint32 i = 0; i < RANDOM_SEQUENCE_LENGTH; ++i)
		{
			uboData->randFloats0[i] = Cymath::randFloat();
			uboData->randFloats1[i] = Cymath::randFloat();
		}
		uboData->renderTargetWidth = renderTargetTexture->getCreateParams().width;
		uboData->renderTargetHeight = renderTargetTexture->getCreateParams().height;
		uboData->bInvalidateHistory = bCameraHasMoved;

		uniformCBVs[swapchainIndex]->writeToGPU(commandList, uboData, sizeof(PathTracingUniform));

		delete uboData;
	}

	// Resize volatile heaps if needed.
	{
		uint32 materialCBVCount, materialSRVCount;
		gpuScene->queryMaterialDescriptorsCount(swapchainIndex, materialCBVCount, materialSRVCount);

		uint32 requiredVolatiles = 0;
		requiredVolatiles += 4; // render target, scene uniform, path tracing uniform, skybox
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

	if (sceneColorUAV == nullptr)
	{
		sceneColorUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(renderTargetTexture,
			UnorderedAccessViewDesc{
				.format         = renderTargetTexture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{
					.mipSlice   = 0,
					.planeSlice = 0,
				},
			}
		));
	}

	// Create skybox SRV.
	if (skyboxFallbackSRV == nullptr)
	{
		auto blackCube = gTextureManager->getSystemTextureBlackCube()->getGPUResource().get();
		skyboxFallbackSRV = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(blackCube,
			ShaderResourceViewDesc{
				.format              = EPixelFormat::R8G8B8A8_UNORM,
				.viewDimension       = ESRVDimension::TextureCube,
				.textureCube         = TextureCubeSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = 1,
					.minLODClamp     = 0.0f
				}
			}
		));
	}
	if (skyboxSRV == nullptr && scene->skyboxTexture != nullptr)
	{
		skyboxSRV = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(scene->skyboxTexture.get(),
			ShaderResourceViewDesc{
				.format              = EPixelFormat::R8G8B8A8_UNORM,
				.viewDimension       = ESRVDimension::TextureCube,
				.textureCube         = TextureCubeSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = 1,
					.minLODClamp     = 0.0f
				}
			}
		));
	}

	DescriptorHeap* descriptorHeaps[] = { volatileViewHeap.at(swapchainIndex) };

	//////////////////////////////////////////////////////////////////////////
	// Copy descriptors to the volatile heap

	DescriptorHeap* volatileHeap = descriptorHeaps[0];
	const uint32 VOLATILE_DESC_IX_RENDERTARGET = 0;
	//const uint32 VOLATILE_DESC_IX_ACCELSTRUCT = 2; // Directly bound; no table.
	const uint32 VOLATILE_DESC_IX_SCENEUNIFORM = 1;
	const uint32 VOLATILE_DESC_IX_PATHTRACINGUNIFORM = 2;
	const uint32 VOLATILE_DESC_IX_SKYBOX = 3;
	// ... Add more fixed slots if needed
	const uint32 VOLATILE_DESC_IX_MATERIAL_BEGIN = VOLATILE_DESC_IX_SKYBOX + 1;

	uint32 VOLATILE_DESC_IX_MATERIAL_CBV, unusedMaterialCBVCount;
	uint32 VOLATILE_DESC_IX_MATERIAL_SRV, unusedMaterialSRVCount;
	uint32 VOLATILE_DESC_IX_NEXT_FREE;

	auto skyboxSRVWithFallback = (skyboxSRV != nullptr) ? skyboxSRV.get() : skyboxFallbackSRV.get();

	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_RENDERTARGET,
		sceneColorUAV->getSourceHeap(), sceneColorUAV->getDescriptorIndexInHeap());
	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_SCENEUNIFORM,
		sceneUniformBuffer->getSourceHeap(), sceneUniformBuffer->getDescriptorIndexInHeap());
	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_PATHTRACINGUNIFORM,
		uniformCBVs[swapchainIndex]->getSourceHeap(), uniformCBVs[swapchainIndex]->getDescriptorIndexInHeap());
	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_SKYBOX,
		skyboxSRVWithFallback->getSourceHeap(), skyboxSRVWithFallback->getDescriptorIndexInHeap());
	gpuScene->copyMaterialDescriptors(
		swapchainIndex,
		volatileHeap, VOLATILE_DESC_IX_MATERIAL_BEGIN,
		VOLATILE_DESC_IX_MATERIAL_CBV, unusedMaterialCBVCount,
		VOLATILE_DESC_IX_MATERIAL_SRV, unusedMaterialSRVCount,
		VOLATILE_DESC_IX_NEXT_FREE);

	//////////////////////////////////////////////////////////////////////////
	// Set root parameters

	commandList->setComputeRootSignature(globalRootSignature.get());

	commandList->setDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	commandList->setComputeRootDescriptorTable(RootParameters::OutputViewSlot,
		volatileHeap, VOLATILE_DESC_IX_RENDERTARGET);
	commandList->setComputeRootDescriptorSRV(RootParameters::AccelerationStructureSlot,
		raytracingScene->getSRV());
	commandList->setComputeRootDescriptorTable(RootParameters::SceneUniformSlot,
		volatileHeap, VOLATILE_DESC_IX_SCENEUNIFORM);
	commandList->setComputeRootDescriptorTable(RootParameters::PathTracingUniformSlot,
		volatileHeap, VOLATILE_DESC_IX_PATHTRACINGUNIFORM);
	commandList->setComputeRootDescriptorSRV(RootParameters::GlobalIndexBufferSlot,
		gIndexBufferPool->getByteAddressBufferView());
	commandList->setComputeRootDescriptorSRV(RootParameters::GlobalVertexBufferSlot,
		gVertexBufferPool->getByteAddressBufferView());
	commandList->setComputeRootDescriptorSRV(RootParameters::GPUSceneSlot,
		gpuScene->getGPUSceneBufferSRV());
	commandList->setComputeRootDescriptorTable(RootParameters::SkyboxSlot,
		volatileHeap, VOLATILE_DESC_IX_SKYBOX);
	commandList->setComputeRootDescriptorTable(RootParameters::MaterialConstantsSlot,
		volatileHeap, VOLATILE_DESC_IX_MATERIAL_CBV);
	commandList->setComputeRootDescriptorTable(RootParameters::MaterialTexturesSlot,
		volatileHeap, VOLATILE_DESC_IX_MATERIAL_SRV);
	
	commandList->setRaytracingPipelineState(RTPSO.get());
	
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
