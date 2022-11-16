#include "ray_traced_reflections.h"
#include "render_device.h"
#include "swap_chain.h"
#include "pipeline_state.h"
#include "gpu_resource.h"
#include "gpu_resource_view.h"
#include "vertex_buffer_pool.h"
#include "shader.h"
#include "static_mesh.h"

// #todo-wip-rt: Temp include
#include "base_pass.h"
#include "texture_manager.h"
#include "material.h"

// Reference: 'D3D12RaytracingHelloWorld' and 'D3D12RaytracingSimpleLighting' samples in
// https://github.com/microsoft/DirectX-Graphics-Samples

#define RTR_MAX_RECURSION            2
// #todo-wip-rt: 1024 for materials
#define RTR_MAX_VOLATILE_DESCRIPTORS (10 + 1024)
// #todo-wip-rt: Should be variable
#define RTR_MAX_STATIC_MESHES        101

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
		DescriptorRange descRanges[4];
		// indirectSpecular = register(u0, space0)
		// gbuffer          = register(u1, space0)
		descRanges[0].init(EDescriptorRangeType::UAV, 2, 0, 0);
		// sceneUniform     = register(b0, space0)
		descRanges[1].init(EDescriptorRangeType::CBV, 1, 0, 0);
		// material CBVs & SRVs (bindless)
		descRanges[2].init(EDescriptorRangeType::CBV, (uint32)(-1), 0, 3); // register(b0, space3)
		descRanges[3].init(EDescriptorRangeType::SRV, (uint32)(-1), 0, 3); // register(t0, space3)

		// https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits
		// Let's be careful of root signature limit as my parameters are growing a little bit...
		// max size         = 64 dwords
		// descriptor table = 1 dword
		// root constant    = 1 dword
		// root descriptor  = 2 dwords

		RootParameter rootParameters[RTRRootParameters::Count];
		rootParameters[RTRRootParameters::OutputViewSlot].initAsDescriptorTable(1, &descRanges[0]);
		rootParameters[RTRRootParameters::AccelerationStructureSlot].initAsSRV(0, 0); // register(t0, space0)
		rootParameters[RTRRootParameters::SceneUniformSlot].initAsDescriptorTable(1, &descRanges[1]); // register(b0, space0)
		rootParameters[RTRRootParameters::GlobalIndexBufferSlot].initAsSRV(1, 0); // register(t1, space0)
		rootParameters[RTRRootParameters::GlobalVertexBufferSlot].initAsSRV(2, 0); // register(t2, space0)
		rootParameters[RTRRootParameters::GPUSceneSlot].initAsSRV(3, 0); // register(t3, space0)

		rootParameters[RTRRootParameters::MaterialConstantsSlot].initAsDescriptorTable(1, &descRanges[2]);
		rootParameters[RTRRootParameters::MaterialTexturesSlot].initAsDescriptorTable(1, &descRanges[3]);

		constexpr uint32 NUM_STATIC_SAMPLERS = 1;
		StaticSamplerDesc staticSamplers[NUM_STATIC_SAMPLERS];
		memset(staticSamplers + 0, 0, sizeof(staticSamplers[0]));
		staticSamplers[0].filter = ETextureFilter::MIN_MAG_LINEAR_MIP_POINT;
		staticSamplers[0].addressU = ETextureAddressMode::Wrap;
		staticSamplers[0].addressV = ETextureAddressMode::Wrap;
		staticSamplers[0].addressW = ETextureAddressMode::Wrap;
		staticSamplers[0].shaderVisibility = EShaderVisibility::All;

		RootSignatureDesc sigDesc(
			_countof(rootParameters), rootParameters,
			NUM_STATIC_SAMPLERS, staticSamplers);
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
	const wchar_t* hitGroupName = L"MyHitGroup";
	{
		raygenShader = std::unique_ptr<ShaderStage>(device->createShader(EShaderStage::RT_RAYGEN_SHADER, "RTR_Raygen"));
		closestHitShader = std::unique_ptr<ShaderStage>(device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "RTR_ClosestHit"));
		missShader = std::unique_ptr<ShaderStage>(device->createShader(EShaderStage::RT_MISS_SHADER, "RTR_Miss"));

		raygenShader->loadFromFile(L"rt_reflection.hlsl", "MyRaygenShader");
		closestHitShader->loadFromFile(L"rt_reflection.hlsl", "MyClosestHitShader");
		missShader->loadFromFile(L"rt_reflection.hlsl", "MyMissShader");

		RaytracingPipelineStateObjectDesc desc;
		desc.hitGroupName                 = hitGroupName;
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
	// Hit group shader table
	{
		struct RootArguments
		{
			ClosestHitPushConstants pushConstants;
		};
		uint32 numShaderRecords = RTR_MAX_STATIC_MESHES;

		hitGroupShaderTable = std::unique_ptr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(
				RTPSO.get(), numShaderRecords, sizeof(RootArguments), L"HitGroupShaderTable"));
		for (uint32 i = 0; i < numShaderRecords; ++i)
		{
			uint32 materialID = i;
			RootArguments rootArguments{ materialID };

			hitGroupShaderTable->uploadRecord(i, hitGroupName, &rootArguments, sizeof(rootArguments));
		}
	}

	volatileViewHeaps.resize(swapchainCount);
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::CBV_SRV_UAV;
		desc.numDescriptors = RTR_MAX_VOLATILE_DESCRIPTORS;
		desc.flags = EDescriptorHeapFlags::ShaderVisible;
		desc.nodeMask = 0;

		volatileViewHeaps[i] = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(desc));

		wchar_t debugName[256];
		swprintf_s(debugName, L"RTR_VolatileViewHeap_%u", i);
		volatileViewHeaps[i]->setDebugName(debugName);
	}
}

bool RayTracedReflections::isAvailable() const
{
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void RayTracedReflections::renderRayTracedReflections(
	RenderCommandList* commandList,
	const SceneProxy* scene,
	const Camera* camera,
	ConstantBufferView* sceneUniformBuffer,
	AccelerationStructure* raytracingScene,
	StructuredBuffer* gpuScene,
	Texture* thinGBufferATexture,
	Texture* indirectSpecularTexture,
	uint32 sceneWidth,
	uint32 sceneHeight,
	BasePass* tempBasePass)
{
	if (isAvailable() == false)
	{
		return;
	}

	const uint32 swapchainIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	DescriptorHeap* descriptorHeaps[] = { volatileViewHeaps[swapchainIndex].get() };

	// #todo: Count material payloads
	// Copy material descriptors

	// Copy descriptors to volatile heap
	DescriptorHeap* volatileHeap = descriptorHeaps[0];
	const uint32 VOLATILE_DESC_IX_RENDERTARGET = 0;
	const uint32 VOLATILE_DESC_IX_GBUFFER = 1;
	//const uint32 VOLATILE_DESC_IX_ACCELSTRUCT = 2; // Directly bound; no table.
	const uint32 VOLATILE_DESC_IX_SCENEUNIFORM = 2;
	const uint32 VOLATILE_DESC_IX_MATERIAL_CBV = 3;
	const uint32 VOLATILE_DESC_IX_MATERIAL_SRV = 3 + tempBasePass->getNumMaterialPayloads();
	{
		gRenderDevice->copyDescriptors(1,
			volatileHeap, VOLATILE_DESC_IX_RENDERTARGET,
			indirectSpecularTexture->getSourceUAVHeap(), indirectSpecularTexture->getUAVDescriptorIndex());
		gRenderDevice->copyDescriptors(1,
			volatileHeap, VOLATILE_DESC_IX_GBUFFER,
			thinGBufferATexture->getSourceUAVHeap(), thinGBufferATexture->getUAVDescriptorIndex());
		gRenderDevice->copyDescriptors(1,
			volatileHeap, VOLATILE_DESC_IX_SCENEUNIFORM,
			sceneUniformBuffer->getSourceHeap(), sceneUniformBuffer->getDescriptorIndexInHeap(swapchainIndex));
	}
	// #todo-wip-rt: Temp material binding
	{
		const uint32 LOD = 0;
		uint32 payloadID = 0;
		for (const StaticMesh* mesh : scene->staticMeshes)
		{
			for (const StaticMeshSection& section : mesh->getSections(LOD))
			{
				ConstantBufferView* cbv = tempBasePass->getMaterialCBV(payloadID);
				gRenderDevice->copyDescriptors(1,
					volatileHeap, VOLATILE_DESC_IX_MATERIAL_CBV + payloadID,
					cbv->getSourceHeap(), cbv->getDescriptorIndexInHeap(swapchainIndex));

				Material* material = section.material;
				Texture* albedo = gTextureManager->getSystemTextureGrey2D();
				if (material && material->albedoTexture)
				{
					albedo = material->albedoTexture;
				}
				gRenderDevice->copyDescriptors(
					1,
					volatileHeap, VOLATILE_DESC_IX_MATERIAL_SRV + payloadID,
					albedo->getSourceSRVHeap(), albedo->getSRVDescriptorIndex());
				
				++payloadID;
			}
		}
		CHECK(payloadID == tempBasePass->getNumMaterialPayloads());
	}

	commandList->setComputeRootSignature(globalRootSignature.get());

	commandList->setDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	commandList->setComputeRootDescriptorTable(RTRRootParameters::OutputViewSlot,
		volatileHeap, VOLATILE_DESC_IX_RENDERTARGET);
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::AccelerationStructureSlot,
		raytracingScene->getSRV());
	commandList->setComputeRootDescriptorTable(RTRRootParameters::SceneUniformSlot,
		volatileHeap, VOLATILE_DESC_IX_SCENEUNIFORM);
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GlobalIndexBufferSlot,
		gIndexBufferPool->internal_getPoolBuffer()->getByteAddressView());
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GlobalVertexBufferSlot,
		gVertexBufferPool->internal_getPoolBuffer()->getByteAddressView());
	commandList->setComputeRootDescriptorSRV(RTRRootParameters::GPUSceneSlot,
		gpuScene->getSRV());
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
