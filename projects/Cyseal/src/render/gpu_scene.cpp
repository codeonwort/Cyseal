#include "gpu_scene.h"
#include "gpu_resource.h"
#include "render_device.h"
#include "core/matrix.h"
#include "world/scene.h"

#define MAX_SCENE_ELEMENTS 1024

struct GPUSceneItem
{
	Float4x4 modelTransform;
	vec3 albedoMultiplier; float _pad0;
};

void GPUScene::initialize()
{
	gpuSceneBuffer = std::unique_ptr<StructuredBuffer>(
		gRenderDevice->createStructuredBuffer(MAX_SCENE_ELEMENTS, sizeof(GPUSceneItem)));

	// Root signature
	{
		constexpr uint32 NUM_ROOT_PARAMETERS = 2;
		RootParameter rootParameters[NUM_ROOT_PARAMETERS];
		rootParameters[0].initAsConstants(0 /*b0*/, 0, 1); // numElements
		rootParameters[1].initAsUAV(0, 0);                 // gpuSceneBuffer

		RootSignatureDesc rootSigDesc(
			NUM_ROOT_PARAMETERS,
			rootParameters,
			0, nullptr,
			ERootSignatureFlags::None);
		rootSignature = std::unique_ptr<RootSignature>(gRenderDevice->createRootSignature(rootSigDesc));
	}

	// Shader
	ShaderStage* shaderCS = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "GPUSceneCS");
	shaderCS->loadFromFile(L"gpu_scene.hlsl", "mainCS");

	// PSO
	{
		ComputePipelineDesc desc;
		desc.rootSignature = rootSignature.get();
		desc.cs = shaderCS;

		pipelineState = std::unique_ptr<PipelineState>(gRenderDevice->createComputePipelineState(desc));
	}
}

void GPUScene::renderGPUScene(RenderCommandList* commandList, const SceneProxy* scene, const Camera* camera)
{
	const uint32 numObjects = (uint32)scene->staticMeshes.size();
	
	// #todo-wip: Upload scene to GPU
	// ...
	
	ResourceBarrier barrierBefore{
		EResourceBarrierType::Transition,
		gpuSceneBuffer.get(),
		EGPUResourceState::COMMON,
		EGPUResourceState::UNORDERED_ACCESS
	};
	commandList->resourceBarriers(1, &barrierBefore);

	commandList->setPipelineState(pipelineState.get());
	commandList->setComputeRootSignature(rootSignature.get());

	commandList->setComputeRootConstant32(0, numObjects, 0);
	commandList->setComputeRootDescriptorUAV(1, gpuSceneBuffer->getUAV());

	commandList->dispatchCompute(numObjects, 1, 1);

	ResourceBarrier barrierAfter{
		EResourceBarrierType::Transition,
		gpuSceneBuffer.get(),
		EGPUResourceState::UNORDERED_ACCESS,
		EGPUResourceState::PIXEL_SHADER_RESOURCE
	};
	commandList->resourceBarriers(1, &barrierAfter);
}

StructuredBuffer* GPUScene::getGPUSceneBuffer() const
{
	return gpuSceneBuffer.get();
}
