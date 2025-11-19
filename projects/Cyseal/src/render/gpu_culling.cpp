#include "gpu_culling.h"
#include "gpu_scene.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "util/logging.h"

DEFINE_LOG_CATEGORY_STATIC(LogGPUCulling);

void GPUCulling::initialize(RenderDevice* inRenderDevice, uint32 inMaxBasePassPermutation)
{
	maxBasePassPermutation = inMaxBasePassPermutation;

	const uint32 swapchainCount = inRenderDevice->getSwapChain()->getBufferCount();

	passDescriptor.initialize(L"GPUCulling", swapchainCount, 0);

	// Shader
	ShaderStage* gpuCullingShader = inRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "GPUCullingCS");
	gpuCullingShader->declarePushConstants({ { "pushConstants", 1} });
	gpuCullingShader->loadFromFile(L"gpu_culling.hlsl", "mainCS");

	pipelineState = UniquePtr<ComputePipelineState>(inRenderDevice->createComputePipelineState(
		ComputePipelineDesc{ .cs = gpuCullingShader, .nodeMask = 0, }
	));

	delete gpuCullingShader; // No use after PSO creation.
}

void GPUCulling::resetCullingResources()
{
	descriptorIndexTracker.lastIndex = 0;
}

void GPUCulling::cullDrawCommands(RenderCommandList* commandList, uint32 swapchainIndex, const GPUCullingInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, GPUCulling);

	auto sceneUniform                = passInput.sceneUniform;
	auto camera                      = passInput.camera;
	auto gpuScene                    = passInput.gpuScene;
	uint32 maxDrawCommands           = passInput.maxDrawCommands;
	Buffer* indirectDrawBuffer       = passInput.indirectDrawBuffer;
	Buffer* culledIndirectDrawBuffer = passInput.culledIndirectDrawBuffer;
	Buffer* drawCounterBuffer        = passInput.drawCounterBuffer;
	auto indirectDrawBufferSRV       = passInput.indirectDrawBufferSRV;
	auto culledIndirectDrawBufferUAV = passInput.culledIndirectDrawBufferUAV;
	auto drawCounterBufferUAV        = passInput.drawCounterBufferUAV;

	uint32 zeroValue = 0;
	drawCounterBuffer->singleWriteToGPU(commandList, &zeroValue, sizeof(zeroValue), 0);

	BufferBarrierAuto barriersBefore[] = {
		{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, indirectDrawBuffer },
		{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, culledIndirectDrawBuffer },
		{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, drawCounterBuffer },
	};
	commandList->barrierAuto(_countof(barriersBefore), barriersBefore, 0, nullptr, 0, nullptr);

	ShaderParameterTable SPT{};
	SPT.pushConstant("pushConstants", maxDrawCommands);
	SPT.constantBuffer("sceneUniform", sceneUniform);
	SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
	SPT.structuredBuffer("drawCommandBuffer", indirectDrawBufferSRV);
	SPT.rwStructuredBuffer("culledDrawCommandBuffer", culledIndirectDrawBufferUAV);
	SPT.rwBuffer("drawCounterBuffer", drawCounterBufferUAV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	requiredVolatiles *= maxBasePassPermutation; // #todo-gpuscene: Optimize if permutation blows up
	passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);
	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);

	commandList->setComputePipelineState(pipelineState.get());
	commandList->bindComputeShaderParameters(pipelineState.get(), &SPT, volatileHeap, &descriptorIndexTracker);
	commandList->dispatchCompute(maxDrawCommands, 1, 1);

	BufferBarrierAuto barriersAfter[] = {
		{ EBarrierSync::EXECUTE_INDIRECT, EBarrierAccess::INDIRECT_ARGUMENT, indirectDrawBuffer },
		{ EBarrierSync::EXECUTE_INDIRECT, EBarrierAccess::INDIRECT_ARGUMENT, culledIndirectDrawBuffer },
		{ EBarrierSync::EXECUTE_INDIRECT, EBarrierAccess::INDIRECT_ARGUMENT, drawCounterBuffer },
	};
	commandList->barrierAuto(_countof(barriersAfter), barriersAfter, 0, nullptr, 0, nullptr);
}
