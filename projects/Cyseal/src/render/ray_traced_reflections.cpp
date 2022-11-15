#include "ray_traced_reflections.h"
#include "render_device.h"
#include "swap_chain.h"
#include "pipeline_state.h"
#include "gpu_resource.h"
#include "gpu_resource_view.h"
#include "vertex_buffer_pool.h"
#include "shader.h"

// Reference: 'D3D12RaytracingHelloWorld' and 'D3D12RaytracingSimpleLighting' samples in
// https://github.com/microsoft/DirectX-Graphics-Samples

#define RTR_MAX_RECURSION            2
#define RTR_MAX_VOLATILE_DESCRIPTORS 10

namespace RTRRootParameters
{
	enum Value
	{
		OutputViewSlot = 0,
		AccelerationStructureSlot,
		SceneUniformSlot,
		GlobalIndexBufferSlot,
		GlobalVertexBufferSlot,
		Count
	};
}

struct RTRViewport
{
	float left;
	float top;
	float right;
	float bottom;
};

struct RayGenConstantBuffer
{
	RTRViewport viewport;
	Float4x4 viewMatrix;
};
static_assert(sizeof(RayGenConstantBuffer) % 4 == 0);

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
		DescriptorRange descRanges[2];
		// indirectSpecular = register(u0, space0)
		// gbuffer          = register(u1, space0)
		descRanges[0].init(EDescriptorRangeType::UAV, 2, 0, 0);
		// sceneUniform     = register(b0, space0)
		descRanges[1].init(EDescriptorRangeType::CBV, 1, 0, 0);

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

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
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
		desc.raygenShader                 = raygenShader.get();
		desc.closestHitShader             = closestHitShader.get();
		desc.missShader                   = missShader.get();
		desc.raygenLocalRootSignature     = raygenLocalRootSignature.get();
		desc.closestHitLocalRootSignature = nullptr;
		desc.missLocalRootSignature       = nullptr;
		desc.globalRootSignature          = globalRootSignature.get();
		desc.maxPayloadSizeInBytes        = 4 * (3 + 1 + 1); // surfaceNormal, materialID, hitTime
		desc.maxAttributeSizeInBytes      = 4 * 2; // barycentrics
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
		// #todo-wip-rt: Update in every tick
		rootArguments.cb.viewport.left = 0.0f;
		rootArguments.cb.viewport.right = (float)device->getSwapChain()->getBackbufferWidth();
		rootArguments.cb.viewport.top = 0.0f;
		rootArguments.cb.viewport.bottom = (float)device->getSwapChain()->getBackbufferHeight();

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
		uint32 numShaderRecords = 1;
		hitGroupShaderTable = std::unique_ptr<RaytracingShaderTable>(
			device->createRaytracingShaderTable(
				RTPSO.get(), numShaderRecords, 0, L"HitGroupShaderTable"));
		for (uint32 i = 0; i < numShaderRecords; ++i)
		{
			hitGroupShaderTable->uploadRecord(i, hitGroupName, nullptr, 0);
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
	Texture* thinGBufferATexture,
	Texture* indirectSpecularTexture,
	uint32 sceneWidth,
	uint32 sceneHeight)
{
	if (isAvailable() == false)
	{
		return;
	}

	const uint32 swapchainIndex = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();
	DescriptorHeap* descriptorHeaps[] = { volatileViewHeaps[swapchainIndex].get() };

	// Copy descriptors to volatile heap
	DescriptorHeap* volatileHeap = descriptorHeaps[0];
	const uint32 VOLATILE_DESC_IX_RENDERTARGET = 0;
	const uint32 VOLATILE_DESC_IX_GBUFFER = 1;
	//const uint32 VOLATILE_DESC_IX_ACCELSTRUCT = 2; // Directly bound; no table.
	const uint32 VOLATILE_DESC_IX_SCENEUNIFORM = 2;
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
