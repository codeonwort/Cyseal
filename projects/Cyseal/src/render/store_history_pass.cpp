#include "store_history_pass.h"
#include "rhi/render_command.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"

void StoreHistoryPass::initialize(RenderDevice* renderDevice)
{
	const uint32 swapchainCount = renderDevice->getSwapChain()->getBufferCount();
	passDescriptor.initialize(L"StoreHistoryPass", swapchainCount, 0);

	// Shader
	ShaderStage* copyCS = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "StoreHistoryCS");
	copyCS->declarePushConstants({ { "pushConstants", 2 } });
	copyCS->loadFromFile(L"store_history.hlsl", "mainCS");

	copyPipeline = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
		ComputePipelineDesc{
			.cs = copyCS,
			.nodeMask = 0,
		}
	));

	delete copyCS; // No use after PSO creation.
}

void StoreHistoryPass::renderHistory(RenderCommandList* commandList, uint32 swapchainIndex, const StoreHistoryPassInput& passInput)
{
	TextureBarrierAuto textureBarriers[] = {
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
			passInput.gbuffer0, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
			passInput.gbuffer1, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			passInput.prevNormalTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			passInput.prevRoughnessTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	ShaderParameterTable SPT{};
	SPT.pushConstants("pushConstants", { passInput.textureWidth, passInput.textureHeight }, 0);
	SPT.texture("gbuffer0", passInput.gbuffer0SRV);
	SPT.texture("gbuffer1", passInput.gbuffer1SRV);
	SPT.rwTexture("rwPrevNormal", passInput.prevNormalUAV);
	SPT.rwTexture("rwPrevRoughness", passInput.prevRoughnessUAV);

	const uint32 requiredVolatiles = SPT.totalDescriptors();
	passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

	commandList->setComputePipelineState(copyPipeline.get());

	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);
	commandList->bindComputeShaderParameters(copyPipeline.get(), &SPT, volatileHeap);

	uint32 dispatchX = (passInput.textureWidth + 7) / 8;
	uint32 dispatchY = (passInput.textureHeight + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);
}
