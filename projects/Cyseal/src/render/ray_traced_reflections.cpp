#include "ray_traced_reflections.h"
#include "render_device.h"
#include "swap_chain.h"
#include "pipeline_state.h"
#include "shader.h"

// Reference: 'D3D12RaytracingHelloWorld' sample in
// https://github.com/microsoft/DirectX-Graphics-Samples

#define RTR_MAX_RECURSION            2
#define RTR_MAX_VOLATILE_DESCRIPTORS 10

namespace RTRRootParameters
{
	enum Value
	{
		OutputViewSlot = 0,
		AccelerationStructureSlot,
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
		DescriptorRange descRanges[1];
		// indirectSpecular = register(u0, space0)
		// gbuffer          = register(u1, space0)
		descRanges[0].init(EDescriptorRangeType::UAV, 2, 0, 0);

		RootParameter rootParameters[RTRRootParameters::Count];
		rootParameters[RTRRootParameters::OutputViewSlot].initAsDescriptorTable(1, &descRanges[0]);
		rootParameters[RTRRootParameters::AccelerationStructureSlot].initAsSRV(0, 0); // register(t0, space0)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
		globalRootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}

	// Local root signature
	{
		RootParameter rootParameters[1];
		rootParameters[0].initAsConstants(0, 0, sizeof(RayGenConstantBuffer) / 4); // register(b0, space0)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
		sigDesc.flags = ERootSignatureFlags::LocalRootSignature;
		localRootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}

	// RTPSO
	{
		ShaderStage* raygenShader = device->createShader(EShaderStage::RT_RAYGEN_SHADER, "RTR_Raygen");
		ShaderStage* closestHitShader = device->createShader(EShaderStage::RT_CLOSESTHIT_SHADER, "RTR_ClosestHit");
		ShaderStage* missShader = device->createShader(EShaderStage::RT_MISS_SHADER, "RTR_Miss");
		raygenShader->loadFromFile(L"rt_reflection.hlsl", "MyRaygenShader");
		closestHitShader->loadFromFile(L"rt_reflection.hlsl", "MyClosestHitShader");
		missShader->loadFromFile(L"rt_reflection.hlsl", "MyMissShader");

		// #todo-wip-rt: RTPSO desc
		RaytracingPipelineStateObjectDesc desc;
		desc.hitGroupName = L"MyHitGroup";
		desc.raygenShader = raygenShader;
		desc.closestHitShader = closestHitShader;
		desc.missShader = missShader;
		desc.raygenLocalRootSignature = localRootSignature.get();
		desc.closestHitLocalRootSignature = nullptr;
		desc.missLocalRootSignature = nullptr;
		desc.globalRootSignature = globalRootSignature.get();
		desc.maxTraceRecursionDepth = RTR_MAX_RECURSION;

		RTPSO = std::unique_ptr<RaytracingPipelineStateObject>(
			gRenderDevice->createRaytracingPipelineStateObject(desc));

		delete raygenShader;
		delete closestHitShader;
		delete missShader;
	}

	// #todo-wip-rt: AS (not here; need an actual scene proxy to build AS)
	// Acceleration structure
	{
		// D3D12_RAYTRACING_GEOMETRY_DESC geomDesc{ ... };
		// D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc{ ... };
		// D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc{ ... };
		// BuildRaytracingAccelerationStructure();
	}

	// #todo-wip-rt: Shader table
	// Shader table
	{
		//
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
	const uint32 VOLATILE_DESC_IX_ACCELSTRUCT = 2;
	// #todo-wip-rt: Replace with TLAS class
	StructuredBuffer* TLAS = nullptr;
	{
		gRenderDevice->copyDescriptors(1,
			volatileHeap, VOLATILE_DESC_IX_RENDERTARGET,
			indirectSpecularTexture->getSourceUAVHeap(), indirectSpecularTexture->getUAVDescriptorIndex());
		gRenderDevice->copyDescriptors(1,
			volatileHeap, VOLATILE_DESC_IX_GBUFFER,
			thinGBufferATexture->getSourceUAVHeap(), thinGBufferATexture->getUAVDescriptorIndex());
		//gRenderDevice->copyDescriptors(1,
		//	volatileHeap, VOLATILE_DESC_IX_ACCELSTRUCT,
		//	TLAS->getSourceSRVHeap(), TLAS->getSRVDescriptorIndex());
	}

	commandList->setComputeRootSignature(globalRootSignature.get());

	commandList->setDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	commandList->setComputeRootDescriptorTable(RTRRootParameters::OutputViewSlot,
		volatileHeap, VOLATILE_DESC_IX_RENDERTARGET);
	//commandList->setComputeRootDescriptorSRV(RTRRootParameters::AccelerationStructureSlot, TLAS->getSRV());
	
	commandList->setRaytracingPipelineState(RTPSO.get());
	
	DispatchRaysDesc dispatchDesc;
	dispatchDesc.raygenShaderTable = raygenShaderTable.get();
	dispatchDesc.missShaderTable = missShaderTable.get();
	dispatchDesc.hitGroupTable = hitGroupTable.get();
	dispatchDesc.width = sceneWidth;
	dispatchDesc.height = sceneHeight;
	dispatchDesc.depth = 1;
	commandList->dispatchRays(dispatchDesc);
}
