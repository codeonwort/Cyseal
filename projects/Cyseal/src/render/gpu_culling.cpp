#include "gpu_culling.h"
#include "gpu_scene.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "util/logging.h"

DEFINE_LOG_CATEGORY_STATIC(LogGPUCulling);

void GPUCulling::initialize()
{
	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	totalVolatileDescriptor.resize(swapchainCount, 0);
	volatileViewHeap.initialize(swapchainCount);

	// Shader
	ShaderStage* gpuCullingShader = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "GPUCullingCS");
	gpuCullingShader->declarePushConstants({ "pushConstants" });
	gpuCullingShader->loadFromFile(L"gpu_culling.hlsl", "mainCS");

	pipelineState = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
		ComputePipelineDesc{
			.cs = gpuCullingShader,
			.nodeMask = 0,
		}
	));

	delete gpuCullingShader; // No use after PSO creation.
}

void GPUCulling::cullDrawCommands(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	ConstantBufferView* sceneUniform,
	const Camera* camera,
	GPUScene* gpuScene,
	uint32 maxDrawCommands,
	Buffer* indirectDrawBuffer,
	ShaderResourceView* indirectDrawBufferSRV,
	Buffer* culledIndirectDrawBuffer,
	UnorderedAccessView* culledIndirectDrawBufferUAV,
	Buffer* drawCounterBuffer,
	UnorderedAccessView* drawCounterBufferUAV)
{
	SCOPED_DRAW_EVENT(commandList, GPUCulling);

	// Resize volatile heap if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // scene uniform
		requiredVolatiles += 1; // gpu scene
		requiredVolatiles += 1; // draw command buffer
		requiredVolatiles += 1; // culled draw command buffer
		requiredVolatiles += 1; // draw counter buffer
		if (requiredVolatiles > totalVolatileDescriptor[swapchainIndex])
		{
			resizeVolatileHeap(swapchainIndex, requiredVolatiles);
		}
	}

	uint32 zeroValue = 0;
	drawCounterBuffer->singleWriteToGPU(commandList, &zeroValue, sizeof(zeroValue), 0);

	BufferMemoryBarrier barriersBefore[] = {
		{
			EBufferMemoryLayout::COMMON,
			EBufferMemoryLayout::PIXEL_SHADER_RESOURCE,
			indirectDrawBuffer,
		},
		{
			EBufferMemoryLayout::COMMON,
			EBufferMemoryLayout::UNORDERED_ACCESS,
			culledIndirectDrawBuffer,
		},
		{
			EBufferMemoryLayout::COMMON,
			EBufferMemoryLayout::UNORDERED_ACCESS,
			drawCounterBuffer,
		},
	};
	commandList->resourceBarriers(_countof(barriersBefore), barriersBefore, 0, nullptr);

	ShaderParameterTable SPT{};
	SPT.pushConstant("pushConstants", maxDrawCommands);
	SPT.constantBuffer("sceneUniform", sceneUniform);
	SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
	SPT.structuredBuffer("drawCommandBuffer", indirectDrawBufferSRV);
	SPT.rwStructuredBuffer("culledDrawCommandBuffer", culledIndirectDrawBufferUAV);
	SPT.rwBuffer("drawCounterBuffer", drawCounterBufferUAV);

	commandList->setComputePipelineState(pipelineState.get());
	commandList->bindComputeShaderParameters(pipelineState.get(), &SPT, volatileViewHeap.at(swapchainIndex));
	commandList->dispatchCompute(maxDrawCommands, 1, 1);

	BufferMemoryBarrier barriersAfter[] = {
		{
			EBufferMemoryLayout::PIXEL_SHADER_RESOURCE,
			EBufferMemoryLayout::INDIRECT_ARGUMENT,
			indirectDrawBuffer,
		},
		{
			EBufferMemoryLayout::UNORDERED_ACCESS,
			EBufferMemoryLayout::INDIRECT_ARGUMENT,
			culledIndirectDrawBuffer,
		},
		{
			EBufferMemoryLayout::UNORDERED_ACCESS,
			EBufferMemoryLayout::INDIRECT_ARGUMENT,
			drawCounterBuffer,
		},
	};
	commandList->resourceBarriers(_countof(barriersAfter), barriersAfter, 0, nullptr);
}

void GPUCulling::resizeVolatileHeap(uint32 swapchainIndex, uint32 maxDescriptors)
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
	swprintf_s(debugName, L"GPUCulling_VolatileViewHeap_%u", swapchainIndex);
	volatileViewHeap[swapchainIndex]->setDebugName(debugName);

	CYLOG(LogGPUCulling, Log, L"Resize volatile heap [%u]: %u descriptors", swapchainIndex, maxDescriptors);
}
