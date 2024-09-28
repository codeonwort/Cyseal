#include "ray_traced_reflections.h"
#include "static_mesh.h"
#include "gpu_scene.h"

#include "util/logging.h"

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

// #wip-dxc-reflection: Refactor RTR
#define REFACTOR 1

// Reference: 'D3D12RaytracingHelloWorld' and 'D3D12RaytracingSimpleLighting' samples in
// https://github.com/microsoft/DirectX-Graphics-Samples

// I don't call TraceRays() recursively, so this constant actually doesn't matter.
// Rather see MAX_BOUNCE in rt_reflection.hlsl.
#define RTR_MAX_RECURSION            2
#define RTR_HIT_GROUP_NAME           L"RTR_HitGroup"

DEFINE_LOG_CATEGORY_STATIC(LogRayTracedReflections);

#if !REFACTOR
namespace RTRRootParameters
{
	enum Value
	{
		OutputViewSlot = 0,
		AccelerationStructureSlot,
		SceneUniformSlot,
		GlobalIndexBufferSlot,
		GlobalVertexBufferSlot,
		GPUSceneSlot,
		SkyboxSlot,
		MaterialConstantsSlot,
		MaterialTexturesSlot,
		Count
	};
}
#endif

// Just to calculate size in bytes.
// Should match with RayPayload in rt_reflection.hlsl.
struct RTRRayPayload
{
	float  surfaceNormal[3];
	float  roughness;
	float  albedo[3];
	float  hitTime;
	uint32 objectID;
};
// Just to calculate size in bytes.
// Should match with MyAttributes in rt_reflection.hlsl.
struct RTRTriangleIntersectionAttributes
{
	float texcoord[2];
};

struct ClosestHitPushConstants
{
	uint32 materialID;
};
static_assert(sizeof(ClosestHitPushConstants) % 4 == 0);

void RayTracedReflections::initialize()
{
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Ray Traced Reflections will be disabled.");
		return;
	}

	RenderDevice* device = gRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	totalVolatileDescriptor.resize(swapchainCount, 0);
	volatileViewHeap.initialize(swapchainCount);

	totalHitGroupShaderRecord.resize(swapchainCount, 0);
	hitGroupShaderTable.initialize(swapchainCount);

#if !REFACTOR
	// Global root signature
	{
		DescriptorRange descRanges[5];
		// indirectSpecular = register(u0, space0)
		// gbuffer          = register(u1, space0)
		descRanges[0].init(EDescriptorRangeType::UAV, 2, 0, 0);
		// sceneUniform     = register(b0, space0)
		descRanges[1].init(EDescriptorRangeType::CBV, 1, 0, 0);
		// skybox
		descRanges[2].init(EDescriptorRangeType::SRV, 1, 4, 0); // register(t4, space0)
		// material CBVs & SRVs (bindless)
		descRanges[3].init(EDescriptorRangeType::CBV, (uint32)(-1), 0, 3); // register(b0, space3)
		descRanges[4].init(EDescriptorRangeType::SRV, (uint32)(-1), 0, 3); // register(t0, space3)

		// https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits
		// Let's be careful of root signature limit as my parameters are growing a little bit...
		// max size         = 64 dwords
		// descriptor table = 1 dword
		// root constant    = 1 dword
		// root descriptor  = 2 dwords

		RootParameter rootParameters[RTRRootParameters::Count];
		rootParameters[RTRRootParameters::OutputViewSlot].initAsDescriptorTable(1, &descRanges[0]);
		rootParameters[RTRRootParameters::AccelerationStructureSlot].initAsSRVBuffer(0, 0);           // register(t0, space0)
		rootParameters[RTRRootParameters::SceneUniformSlot].initAsDescriptorTable(1, &descRanges[1]); // register(b0, space0)
		rootParameters[RTRRootParameters::GlobalIndexBufferSlot].initAsSRVBuffer(1, 0);               // register(t1, space0)
		rootParameters[RTRRootParameters::GlobalVertexBufferSlot].initAsSRVBuffer(2, 0);              // register(t2, space0)
		rootParameters[RTRRootParameters::GPUSceneSlot].initAsSRVBuffer(3, 0);                        // register(t3, space0)
		rootParameters[RTRRootParameters::SkyboxSlot].initAsDescriptorTable(1, &descRanges[2]);       // register(t4, space0)

		rootParameters[RTRRootParameters::MaterialConstantsSlot].initAsDescriptorTable(1, &descRanges[3]);
		rootParameters[RTRRootParameters::MaterialTexturesSlot].initAsDescriptorTable(1, &descRanges[4]);

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
#endif

#if !REFACTOR
	{
		RootParameter rootParameters[1];
		rootParameters[0].initAsConstants(0, 2, sizeof(ClosestHitPushConstants) / 4); // register(b0, space2)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
		sigDesc.flags = ERootSignatureFlags::LocalRootSignature;
		closestHitLocalRootSignature = UniquePtr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}
#endif

	// RTPSO
	{
		raygenShader = UniquePtr<ShaderStage>(device->createShader(EShaderStage::RT_RAYGEN_SHADER, "RTR_Raygen"));
		closestHitShader = UniquePtr<ShaderStage>(device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "RTR_ClosestHit"));
		missShader = UniquePtr<ShaderStage>(device->createShader(EShaderStage::RT_MISS_SHADER, "RTR_Miss"));
		raygenShader->declarePushConstants();
		closestHitShader->declarePushConstants({ "g_closestHitCB" });
		missShader->declarePushConstants();
		raygenShader->loadFromFile(L"rt_reflection.hlsl", "MyRaygenShader");
		closestHitShader->loadFromFile(L"rt_reflection.hlsl", "MyClosestHitShader");
		missShader->loadFromFile(L"rt_reflection.hlsl", "MyMissShader");

#if REFACTOR
		RaytracingPipelineStateObjectDesc2 pipelineDesc{
			.hitGroupName                 = RTR_HIT_GROUP_NAME,
			.hitGroupType                 = ERaytracingHitGroupType::Triangles,
			.raygenShader                 = raygenShader.get(),
			.closestHitShader             = closestHitShader.get(),
			.missShader                   = missShader.get(),
			.raygenLocalParameters        = {},
			.closestHitLocalParameters    = { "g_closestHitCB" },
			.missLocalParameters          = {},
			.maxPayloadSizeInBytes        = sizeof(RTRRayPayload),
			.maxAttributeSizeInBytes      = sizeof(RTRTriangleIntersectionAttributes),
			.maxTraceRecursionDepth       = RTR_MAX_RECURSION,
		};
		RTPSO = UniquePtr<RaytracingPipelineStateObject>(gRenderDevice->createRaytracingPipelineStateObject(pipelineDesc));
#else
		RaytracingPipelineStateObjectDesc pipelineDesc{
			.hitGroupName                 = RTR_HIT_GROUP_NAME,
			.hitGroupType                 = ERaytracingHitGroupType::Triangles,
			.raygenShader                 = raygenShader.get(),
			.closestHitShader             = closestHitShader.get(),
			.missShader                   = missShader.get(),
			.raygenLocalRootSignature     = nullptr,
			.closestHitLocalRootSignature = closestHitLocalRootSignature.get(),
			.missLocalRootSignature       = nullptr,
			.globalRootSignature          = globalRootSignature.get(),
			.maxPayloadSizeInBytes        = sizeof(RTRRayPayload),
			.maxAttributeSizeInBytes      = sizeof(RTRTriangleIntersectionAttributes),
			.maxTraceRecursionDepth       = RTR_MAX_RECURSION,
		};
		RTPSO = UniquePtr<RaytracingPipelineStateObject>(gRenderDevice->createRaytracingPipelineStateObject(pipelineDesc));
#endif
	}

	// Acceleration Structure is built by SceneRenderer.
	// ...

	// Raygen shader table
	{
		uint32 numShaderRecords = 1;
		raygenShaderTable = UniquePtr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(RTPSO.get(), numShaderRecords, 0, L"RayGenShaderTable"));
		raygenShaderTable->uploadRecord(0, raygenShader.get(), nullptr, 0);
	}
	// Miss shader table
	{
		uint32 numShaderRecords = 1;
		missShaderTable = UniquePtr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(RTPSO.get(), numShaderRecords, 0, L"MissShaderTable"));
		missShaderTable->uploadRecord(0, missShader.get(), nullptr, 0);
	}
	// Hit group shader table is created in resizeHitGroupShaderTable().
	// ...

	// Cleanup
	{
		raygenShader.reset();
		closestHitShader.reset();
		missShader.reset();
	}
}

bool RayTracedReflections::isAvailable() const
{
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void RayTracedReflections::renderRayTracedReflections(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const SceneProxy* scene,
	const Camera* camera,
	ConstantBufferView* sceneUniformBuffer,
	AccelerationStructure* raytracingScene,
	GPUScene* gpuScene,
	Texture* thinGBufferATexture,
	Texture* indirectSpecularTexture,
	uint32 sceneWidth,
	uint32 sceneHeight)
{
	if (isAvailable() == false)
	{
		return;
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
		requiredVolatiles += 1; // gbufferA
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += materialCBVCount; // materials[]
		requiredVolatiles += materialSRVCount; // albedoTextures[]

		if (requiredVolatiles > totalVolatileDescriptor[swapchainIndex])
		{
			resizeVolatileHeap(swapchainIndex, requiredVolatiles);
		}
	}

	// Resize hit group shader table if needed.
	{
		uint32 requiredRecordCount = scene->totalMeshSectionsLOD0; // #todo-lod
		if (requiredRecordCount > totalHitGroupShaderRecord[swapchainIndex])
		{
			resizeHitGroupShaderTable(swapchainIndex, requiredRecordCount);
		}
	}

	if (indirectSpecularUAV == nullptr)
	{
		indirectSpecularUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(indirectSpecularTexture,
			UnorderedAccessViewDesc{
				.format         = indirectSpecularTexture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{
					.mipSlice   = 0,
					.planeSlice = 0,
				},
			}
		));
	}
	if (thinGBufferAUAV == nullptr)
	{
		thinGBufferAUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(thinGBufferATexture,
			UnorderedAccessViewDesc{
				.format         = thinGBufferATexture->getCreateParams().format,
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

#if !REFACTOR
	const uint32 VOLATILE_DESC_IX_RENDERTARGET = 0;
	const uint32 VOLATILE_DESC_IX_GBUFFER = 1;
	//const uint32 VOLATILE_DESC_IX_ACCELSTRUCT = 2; // Directly bound; no table.
	const uint32 VOLATILE_DESC_IX_SCENEUNIFORM = 2;
	const uint32 VOLATILE_DESC_IX_SKYBOX = 3;
	// ... Add more fixed slots if needed
	const uint32 VOLATILE_DESC_IX_MATERIAL_BEGIN = VOLATILE_DESC_IX_SKYBOX + 1;

	uint32 VOLATILE_DESC_IX_MATERIAL_CBV, unusedMaterialCBVCount;
	uint32 VOLATILE_DESC_IX_MATERIAL_SRV, unusedMaterialSRVCount;
	uint32 VOLATILE_DESC_IX_NEXT_FREE;
#endif

	auto skyboxSRVWithFallback = (skyboxSRV != nullptr) ? skyboxSRV.get() : skyboxFallbackSRV.get();

#if !REFACTOR
	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_RENDERTARGET,
		indirectSpecularUAV->getSourceHeap(), indirectSpecularUAV->getDescriptorIndexInHeap());
	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_GBUFFER,
		thinGBufferAUAV->getSourceHeap(), thinGBufferAUAV->getDescriptorIndexInHeap());
	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_SCENEUNIFORM,
		sceneUniformBuffer->getSourceHeap(), sceneUniformBuffer->getDescriptorIndexInHeap());
	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_SKYBOX,
		skyboxSRVWithFallback->getSourceHeap(), skyboxSRVWithFallback->getDescriptorIndexInHeap());
	gpuScene->copyMaterialDescriptors(
		swapchainIndex,
		volatileHeap, VOLATILE_DESC_IX_MATERIAL_BEGIN,
		VOLATILE_DESC_IX_MATERIAL_CBV, unusedMaterialCBVCount,
		VOLATILE_DESC_IX_MATERIAL_SRV, unusedMaterialSRVCount,
		VOLATILE_DESC_IX_NEXT_FREE);
#endif

	commandList->setRaytracingPipelineState(RTPSO.get());
#if REFACTOR
	// Bind global shader parameters.
	{
		GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors(swapchainIndex);

		ShaderParameterTable SPT{};
		SPT.accelerationStructure("rtScene", raytracingScene->getSRV());
		SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
		SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.texture("skybox", skyboxSRVWithFallback);
		SPT.rwTexture("renderTarget", indirectSpecularUAV.get());
		SPT.constantBuffer("sceneUniform", sceneUniformBuffer);
		// Bindless
		SPT.constantBuffer("materials", gpuSceneDesc.cbvHeap, 0, gpuSceneDesc.cbvCount);
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		commandList->bindRaytracingShaderParameters(RTPSO.get(), &SPT, volatileHeap);
	}
#else
	commandList->setComputeRootSignature(globalRootSignature.get());
	commandList->setDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	commandList->setComputeRootDescriptorTable(RTRRootParameters::OutputViewSlot, volatileHeap, VOLATILE_DESC_IX_RENDERTARGET);
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::AccelerationStructureSlot, raytracingScene->getSRV());
	commandList->setComputeRootDescriptorTable(RTRRootParameters::SceneUniformSlot, volatileHeap, VOLATILE_DESC_IX_SCENEUNIFORM);
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GlobalIndexBufferSlot, gIndexBufferPool->getByteAddressBufferView());
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GlobalVertexBufferSlot, gVertexBufferPool->getByteAddressBufferView());
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GPUSceneSlot, gpuScene->getGPUSceneBufferSRV());
	commandList->setComputeRootDescriptorTable(RTRRootParameters::SkyboxSlot, volatileHeap, VOLATILE_DESC_IX_SKYBOX);
	commandList->setComputeRootDescriptorTable(RTRRootParameters::MaterialConstantsSlot, volatileHeap, VOLATILE_DESC_IX_MATERIAL_CBV);
	commandList->setComputeRootDescriptorTable(RTRRootParameters::MaterialTexturesSlot, volatileHeap, VOLATILE_DESC_IX_MATERIAL_SRV);
#endif
	
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

void RayTracedReflections::resizeVolatileHeap(uint32 swapchainIndex, uint32 maxDescriptors)
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
	swprintf_s(debugName, L"RTR_VolatileViewHeap_%u", swapchainIndex);
	volatileViewHeap[swapchainIndex]->setDebugName(debugName);

	CYLOG(LogRayTracedReflections, Log, L"Resize volatile heap [%u]: %u descriptors", swapchainIndex, maxDescriptors);
}

void RayTracedReflections::resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords)
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
			.pushConstants = ClosestHitPushConstants{ .materialID = i }
		};

		hitGroupShaderTable[swapchainIndex]->uploadRecord(i, RTR_HIT_GROUP_NAME, &rootArguments, sizeof(rootArguments));
	}

	CYLOG(LogRayTracedReflections, Log, L"Resize hit group shader table [%u]: %u records", swapchainIndex, maxRecords);
}
