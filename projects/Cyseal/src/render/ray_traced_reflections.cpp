#include "ray_traced_reflections.h"
#include "render_device.h"
#include "pipeline_state.h"
#include "shader.h"

// Reference: 'D3D12RaytracingHelloWorld' sample in
// https://github.com/microsoft/DirectX-Graphics-Samples

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

	// Global root signature
	{
		RootParameter rootParameters[2];
		rootParameters[0].initAsUAV(0, 0); // register(u0, space0)
		rootParameters[1].initAsSRV(0, 0); // register(t0, space0)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
		globalRootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(sigDesc));
	}

	// Local root signature
	{
		RootParameter rootParameters[1];
		rootParameters[0].initAsConstants(0, 0, sizeof(RayGenConstantBuffer) / 4); // register(b0, space0)

		RootSignatureDesc sigDesc(_countof(rootParameters), rootParameters);
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
		desc.raygenShader = raygenShader;
		desc.closestHitShader = closestHitShader;
		desc.missShader = missShader;

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
}

bool RayTracedReflections::isAvailable() const
{
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void RayTracedReflections::renderRayTracedReflections(
	RenderCommandList* commandList,
	const SceneProxy* scene,
	const Camera* camera)
{
	if (isAvailable() == false)
	{
		return;
	}

	// #todo-wip-rt: DispatchRays
	// SetComputeRootSignature(globalRootSignature);
	// SetDescriptorHeaps();
	// SetComputeRootDescriptorTable();
	// SetComputeRootDescriptorSRV();
	// SetPipelineState1(RTPSO);
	// DispatchRays(dispatchDesc);
}
