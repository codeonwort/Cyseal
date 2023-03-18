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

// Reference: 'D3D12RaytracingHelloWorld' and 'D3D12RaytracingSimpleLighting' samples in
// https://github.com/microsoft/DirectX-Graphics-Samples

#define RTR_MAX_RECURSION            2
#define RTR_HIT_GROUP_NAME           L"RTR_HitGroup"

DEFINE_LOG_CATEGORY_STATIC(LogRayTracedReflections);

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

struct RayGenConstantBuffer
{
	Float4x4 dummyValue;
};
struct ClosestHitPushConstants
{
	uint32 materialID;
};
static_assert(sizeof(RayGenConstantBuffer) % 4 == 0);
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
		rootParameters[RTRRootParameters::AccelerationStructureSlot].initAsSRV(0, 0);                 // register(t0, space0)
		rootParameters[RTRRootParameters::SceneUniformSlot].initAsDescriptorTable(1, &descRanges[1]); // register(b0, space0)
		rootParameters[RTRRootParameters::GlobalIndexBufferSlot].initAsSRV(1, 0);                     // register(t1, space0)
		rootParameters[RTRRootParameters::GlobalVertexBufferSlot].initAsSRV(2, 0);                    // register(t2, space0)
		rootParameters[RTRRootParameters::GPUSceneSlot].initAsSRV(3, 0);                              // register(t3, space0)
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
		globalRootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}

	// Local root signature
	{
		RootParameter rootParameters[1];
		rootParameters[0].initAsConstants(0, 1, sizeof(RayGenConstantBuffer) / 4); // register(b0, space1)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
		sigDesc.flags = ERootSignatureFlags::LocalRootSignature;
		raygenLocalRootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}
	{
		RootParameter rootParameters[1];
		rootParameters[0].initAsConstants(0, 2, sizeof(ClosestHitPushConstants) / 4); // register(b0, space2)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
		sigDesc.flags = ERootSignatureFlags::LocalRootSignature;
		closestHitLocalRootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}

	// RTPSO
	{
		raygenShader = std::unique_ptr<ShaderStage>(device->createShader(EShaderStage::RT_RAYGEN_SHADER, "RTR_Raygen"));
		closestHitShader = std::unique_ptr<ShaderStage>(device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "RTR_ClosestHit"));
		missShader = std::unique_ptr<ShaderStage>(device->createShader(EShaderStage::RT_MISS_SHADER, "RTR_Miss"));

		raygenShader->loadFromFile(L"rt_reflection.hlsl", "MyRaygenShader");
		closestHitShader->loadFromFile(L"rt_reflection.hlsl", "MyClosestHitShader");
		missShader->loadFromFile(L"rt_reflection.hlsl", "MyMissShader");

		RaytracingPipelineStateObjectDesc desc;
		desc.hitGroupName                 = RTR_HIT_GROUP_NAME;
		desc.hitGroupType                 = ERaytracingHitGroupType::Triangles;
		desc.raygenShader                 = raygenShader.get();
		desc.closestHitShader             = closestHitShader.get();
		desc.missShader                   = missShader.get();
		desc.raygenLocalRootSignature     = raygenLocalRootSignature.get();
		desc.closestHitLocalRootSignature = closestHitLocalRootSignature.get();
		desc.missLocalRootSignature       = nullptr;
		desc.globalRootSignature          = globalRootSignature.get();
		desc.maxPayloadSizeInBytes        = sizeof(RTRRayPayload);
		desc.maxAttributeSizeInBytes      = sizeof(RTRTriangleIntersectionAttributes);
		desc.maxTraceRecursionDepth       = RTR_MAX_RECURSION;

		RTPSO = std::unique_ptr<RaytracingPipelineStateObject>(
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
		raygenShaderTable = std::unique_ptr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(
				RTPSO.get(), numShaderRecords, sizeof(rootArguments), L"RayGenShaderTable"));
		raygenShaderTable->uploadRecord(0, raygenShader.get(), &rootArguments, sizeof(rootArguments));
	}
	// Miss shader table
	{
		uint32 numShaderRecords = 1;
		missShaderTable = std::unique_ptr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(
				RTPSO.get(), numShaderRecords, 0, L"MissShaderTable"));
		missShaderTable->uploadRecord(0, missShader.get(), nullptr, 0);
	}
	// Hit group shader table is created in resizeHitGroupShaderTable().
	// ...
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
		gpuScene->queryMaterialDescriptorsCount(materialCBVCount, materialSRVCount);

		uint32 requiredVolatiles = 0;
		requiredVolatiles += 4; // render target, gbufferA, scene uniform, skybox
		requiredVolatiles += materialCBVCount;
		requiredVolatiles += materialSRVCount;

		if (requiredVolatiles > totalVolatileDescriptors)
		{
			resizeVolatileHeaps(requiredVolatiles);
		}
	}

	// Resize hit group shader table if needed.
	{
		uint32 requiredRecordCount = (uint32)scene->staticMeshes.size();
		if (requiredRecordCount > totalHitGroupShaderRecord)
		{
			resizeHitGroupShaderTable(requiredRecordCount);
		}
	}

	// Create skybox SRV.
	if (skyboxFallbackSRV == nullptr)
	{
		auto blackCube = gTextureManager->getSystemTextureBlackCube()->getGPUResource().get();
		skyboxFallbackSRV = std::unique_ptr<ShaderResourceView>(gRenderDevice->createSRV(blackCube,
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
		skyboxSRV = std::unique_ptr<ShaderResourceView>(gRenderDevice->createSRV(scene->skyboxTexture.get(),
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

	DescriptorHeap* descriptorHeaps[] = { volatileViewHeaps[swapchainIndex].get() };

	//////////////////////////////////////////////////////////////////////////
	// Copy descriptors to the volatile heap

	DescriptorHeap* volatileHeap = descriptorHeaps[0];
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

	auto skyboxSRVWithFallback = (skyboxSRV != nullptr) ? skyboxSRV.get() : skyboxFallbackSRV.get();

	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_RENDERTARGET,
		indirectSpecularTexture->getSourceUAVHeap(), indirectSpecularTexture->getUAVDescriptorIndex());
	gRenderDevice->copyDescriptors(1,
		volatileHeap, VOLATILE_DESC_IX_GBUFFER,
		thinGBufferATexture->getSourceUAVHeap(), thinGBufferATexture->getUAVDescriptorIndex());
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

	//////////////////////////////////////////////////////////////////////////
	// Set root parameters

	commandList->setComputeRootSignature(globalRootSignature.get());

	commandList->setDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	commandList->setComputeRootDescriptorTable(RTRRootParameters::OutputViewSlot,
		volatileHeap, VOLATILE_DESC_IX_RENDERTARGET);
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::AccelerationStructureSlot,
		raytracingScene->getSRV());
	commandList->setComputeRootDescriptorTable(RTRRootParameters::SceneUniformSlot,
		volatileHeap, VOLATILE_DESC_IX_SCENEUNIFORM);
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GlobalIndexBufferSlot,
		gIndexBufferPool->getByteAddressBufferView());
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GlobalVertexBufferSlot,
		gVertexBufferPool->getByteAddressBufferView());
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GPUSceneSlot,
		gpuScene->getGPUSceneBufferSRV());
	commandList->setComputeRootDescriptorTable(RTRRootParameters::SkyboxSlot,
		volatileHeap, VOLATILE_DESC_IX_SKYBOX);
	commandList->setComputeRootDescriptorTable(RTRRootParameters::MaterialConstantsSlot,
		volatileHeap, VOLATILE_DESC_IX_MATERIAL_CBV);
	commandList->setComputeRootDescriptorTable(RTRRootParameters::MaterialTexturesSlot,
		volatileHeap, VOLATILE_DESC_IX_MATERIAL_SRV);
	
	commandList->setRaytracingPipelineState(RTPSO.get());
	
	DispatchRaysDesc dispatchDesc;
	dispatchDesc.raygenShaderTable = raygenShaderTable.get();
	dispatchDesc.missShaderTable = missShaderTable.get();
	dispatchDesc.hitGroupTable = hitGroupShaderTable.get();
	dispatchDesc.width = sceneWidth;
	dispatchDesc.height = sceneHeight;
	dispatchDesc.depth = 1;
	commandList->dispatchRays(dispatchDesc);
}

void RayTracedReflections::resizeVolatileHeaps(uint32 maxDescriptors)
{
	totalVolatileDescriptors = maxDescriptors;

	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	volatileViewHeaps.resize(swapchainCount);
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		volatileViewHeaps[i] = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV_SRV_UAV,
				.numDescriptors = maxDescriptors,
				.flags          = EDescriptorHeapFlags::ShaderVisible,
				.nodeMask       = 0,
			}
		));

		wchar_t debugName[256];
		swprintf_s(debugName, L"RTR_VolatileViewHeap_%u", i);
		volatileViewHeaps[i]->setDebugName(debugName);
	}

	CYLOG(LogRayTracedReflections, Log, L"Resize volatile heap: %u descriptors", maxDescriptors);
}

void RayTracedReflections::resizeHitGroupShaderTable(uint32 maxRecords)
{
	totalHitGroupShaderRecord = maxRecords;

	struct RootArguments
	{
		ClosestHitPushConstants pushConstants;
	};

	hitGroupShaderTable = std::unique_ptr<RaytracingShaderTable>(
		gRenderDevice->createRaytracingShaderTable(
			RTPSO.get(), maxRecords, sizeof(RootArguments), L"HitGroupShaderTable"));

	for (uint32 i = 0; i < maxRecords; ++i)
	{
		RootArguments rootArguments{
			.pushConstants = ClosestHitPushConstants{
				.materialID = i
			}
		};

		hitGroupShaderTable->uploadRecord(i, RTR_HIT_GROUP_NAME, &rootArguments, sizeof(rootArguments));
	}

	CYLOG(LogRayTracedReflections, Log, L"Resize hit group shader table: %u records", maxRecords);
}
