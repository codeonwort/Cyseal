#include "store_history_pass.h"
#include "rhi/render_command.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"

#define PF_normalHistory       EPixelFormat::R16G16B16A16_FLOAT
#define PF_roughnessHistory    EPixelFormat::R32_FLOAT

void StoreHistoryPass::initialize(RenderDevice* renderDevice)
{
	const uint32 swapchainCount = renderDevice->getSwapChain()->getBufferCount();
	passDescriptor.initialize(L"StoreHistoryPass", swapchainCount, 0);

	const ETextureAccessFlags historyFlags = ETextureAccessFlags::UAV | ETextureAccessFlags::SRV;
	normalHistory.initialize(PF_normalHistory, historyFlags, L"RT_NormalHistory");
	roughnessHistory.initialize(PF_roughnessHistory, historyFlags, L"RT_RoughnessHistory");

	// Shader
	ShaderStage* copyCS = gRenderDevice->createShader(EShaderStage::COMPUTE_SHADER, "StoreHistoryCS");
	copyCS->declarePushConstants({ { "pushConstants", 2 } });
	copyCS->loadFromFile(L"store_history.hlsl", "mainCS");

	copyPipeline = UniquePtr<ComputePipelineState>(gRenderDevice->createComputePipelineState(
		ComputePipelineDesc{ .cs = copyCS, .nodeMask = 0, }
	));

	delete copyCS; // No use after PSO creation.
}

void StoreHistoryPass::extractCurrent(RenderCommandList* commandList, uint32 swapchainIndex, const StoreHistoryPassInput& passInput)
{
	resizeTextures(commandList, passInput.textureWidth, passInput.textureHeight);

	const uint32 currFrame = swapchainIndex % 2;
	const uint32 prevFrame = (swapchainIndex + 1) % 2;

	auto currNormalTexture    = normalHistory.getTexture(currFrame);
	auto currNormalUAV        = normalHistory.getUAV(currFrame);
	auto currRoughnessTexture = roughnessHistory.getTexture(currFrame);
	auto currRoughnessUAV     = roughnessHistory.getUAV(currFrame);

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
			currNormalTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			currRoughnessTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	ShaderParameterTable SPT{};
	SPT.pushConstants("pushConstants", { passInput.textureWidth, passInput.textureHeight }, 0);
	SPT.texture("gbuffer0", passInput.gbuffer0SRV);
	SPT.texture("gbuffer1", passInput.gbuffer1SRV);
	SPT.rwTexture("rwNormal", currNormalUAV);
	SPT.rwTexture("rwRoughness", currRoughnessUAV);

	const uint32 requiredVolatiles = SPT.totalDescriptors();
	passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

	commandList->setComputePipelineState(copyPipeline.get());

	DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);
	commandList->bindComputeShaderParameters(copyPipeline.get(), &SPT, volatileHeap);

	uint32 dispatchX = (passInput.textureWidth + 7) / 8;
	uint32 dispatchY = (passInput.textureHeight + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);
}

void StoreHistoryPass::copyCurrentToPrev(RenderCommandList* commandList, uint32 swapchainIndex)
{
	const uint32 currFrame = swapchainIndex % 2;
	const uint32 prevFrame = (swapchainIndex + 1) % 2;

	auto currNormal    = normalHistory.getTexture(currFrame);
	auto prevNormal    = normalHistory.getTexture(prevFrame);
	auto currRoughness = roughnessHistory.getTexture(currFrame);
	auto prevRoughness = roughnessHistory.getTexture(prevFrame);

	TextureBarrierAuto textureBarriers[] = {
		{
			EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, EBarrierLayout::CopySource,
			currNormal, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
		{
			EBarrierSync::COPY, EBarrierAccess::COPY_DEST, EBarrierLayout::CopyDest,
			prevNormal, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
		{
			EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, EBarrierLayout::CopySource,
			currRoughness, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
		{
			EBarrierSync::COPY, EBarrierAccess::COPY_DEST, EBarrierLayout::CopyDest,
			prevRoughness, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		},
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	commandList->copyTexture2D(currNormal, prevNormal);
	commandList->copyTexture2D(currRoughness, prevRoughness);
}

StoreHistoryPassResources StoreHistoryPass::getResources(uint32 swapchainIndex) const
{
	const uint32 currFrame = swapchainIndex % 2;
	const uint32 prevFrame = (swapchainIndex + 1) % 2;

	return StoreHistoryPassResources{
		normalHistory.getTexture(currFrame),
		normalHistory.getSRV(currFrame),
		normalHistory.getUAV(currFrame),
		normalHistory.getTexture(prevFrame),
		normalHistory.getSRV(prevFrame),
		normalHistory.getUAV(prevFrame),
		roughnessHistory.getTexture(currFrame),
		roughnessHistory.getSRV(currFrame),
		roughnessHistory.getUAV(currFrame),
		roughnessHistory.getTexture(prevFrame),
		roughnessHistory.getSRV(prevFrame),
		roughnessHistory.getUAV(prevFrame),
	};
}

void StoreHistoryPass::resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight)
{
	if (historyWidth == newWidth && historyHeight == newHeight)
	{
		return;
	}
	historyWidth = newWidth;
	historyHeight = newHeight;

	normalHistory.resizeTextures(commandList, historyWidth, historyHeight);
	roughnessHistory.resizeTextures(commandList, historyWidth, historyHeight);
}
