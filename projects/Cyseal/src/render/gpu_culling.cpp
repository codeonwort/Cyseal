#include "gpu_culling.h"
#include "gpu_scene.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "util/logging.h"

#include <memory>

DEFINE_LOG_CATEGORY_STATIC(LogGPUCulling);

namespace RootParameters
{
	enum Value
	{
		PushConstantsSlot = 0,
		SceneUniformSlot,
		GPUSceneSlot,
		DrawBufferSlot,
		CulledDrawBufferSlot,
		CounterBufferSlot,
		Count
	};
}

void GPUCulling::initialize()
{
	const uint32 swapchainCount = gRenderDevice->getSwapChain()->getBufferCount();

	totalVolatileDescriptor.resize(swapchainCount, 0);
	volatileViewHeap.initialize(swapchainCount);

	// Root signature
	{
		DescriptorRange descriptorRanges[2];
		descriptorRanges[0].init(EDescriptorRangeType::CBV, 1, 1, 0); // register(b1, space0)
		descriptorRanges[1].init(EDescriptorRangeType::UAV, 1, 1, 0); // register(u1, space0)

		RootParameter rootParameters[RootParameters::Count];
		rootParameters[RootParameters::PushConstantsSlot].initAsConstants(0, 0, 1);
		rootParameters[RootParameters::SceneUniformSlot].initAsDescriptorTable(1, &descriptorRanges[0]);
		rootParameters[RootParameters::GPUSceneSlot].initAsSRV(0, 0);           // register(t0, space0)
		rootParameters[RootParameters::DrawBufferSlot].initAsSRV(1, 0);         // register(t1, space0)
		rootParameters[RootParameters::CulledDrawBufferSlot].initAsUAV(0, 0);   // register(u0, space0)
		rootParameters[RootParameters::CounterBufferSlot].initAsDescriptorTable(1, &descriptorRanges[1]);

		RootSignatureDesc rootSigDesc(
			RootParameters::Count,
			rootParameters,
			0, nullptr,
			ERootSignatureFlags::None);
		rootSignature = UniquePtr<RootSignature>(gRenderDevice->createRootSignature(rootSigDesc));
	}

	// Shader
	ShaderStage* shaderCS = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "GPUCullingCS");
	shaderCS->loadFromFile(L"gpu_culling.hlsl", "mainCS");

	// PSO
	pipelineState = UniquePtr<PipelineState>(gRenderDevice->createComputePipelineState(
		ComputePipelineDesc{
			.rootSignature = rootSignature.get(),
			.cs            = shaderCS,
			.nodeMask      = 0
		}
	));

	delete shaderCS;
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
		//requiredVolatiles += 1; // gpu scene
		//requiredVolatiles += 1; // draw command buffer
		//requiredVolatiles += 1; // culled draw command buffer
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

	commandList->setPipelineState(pipelineState.get());
	commandList->setComputeRootSignature(rootSignature.get());

	DescriptorHeap* volatileHeap = volatileViewHeap.at(swapchainIndex);
	DescriptorHeap* heaps[] = { volatileHeap };
	commandList->setDescriptorHeaps(1, heaps);

	constexpr uint32 VOLATILE_IX_SceneUniform = 0;
	constexpr uint32 VOLATILE_IX_DrawCounterBuffer = 1;
	gRenderDevice->copyDescriptors(1, volatileHeap, VOLATILE_IX_SceneUniform,
		sceneUniform->getSourceHeap(), sceneUniform->getDescriptorIndexInHeap());
	gRenderDevice->copyDescriptors(1, volatileHeap, VOLATILE_IX_DrawCounterBuffer,
		drawCounterBufferUAV->getSourceHeap(), drawCounterBufferUAV->getDescriptorIndexInHeap());

	commandList->setComputeRootConstant32(RootParameters::PushConstantsSlot, maxDrawCommands, 0);
	commandList->setComputeRootDescriptorTable(RootParameters::SceneUniformSlot, volatileHeap, VOLATILE_IX_SceneUniform);
	commandList->setComputeRootDescriptorSRV(RootParameters::GPUSceneSlot, gpuScene->gpuSceneBufferSRV.get());
	commandList->setComputeRootDescriptorSRV(RootParameters::DrawBufferSlot, indirectDrawBufferSRV);
	commandList->setComputeRootDescriptorUAV(RootParameters::CulledDrawBufferSlot, culledIndirectDrawBufferUAV);
	commandList->setComputeRootDescriptorTable(RootParameters::CounterBufferSlot, volatileHeap, VOLATILE_IX_DrawCounterBuffer);

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
