#include "null_renderer.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"

#define EMPTY_LOOP 1

void NullRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;
}

void NullRenderer::destroy()
{
	device = nullptr;
}

void NullRenderer::render(const SceneProxy* scene, const Camera* camera, const RendererOptions& renderOptions)
{
#if EMPTY_LOOP
	SwapChain* swapChain      = device->getSwapChain();
	uint32 swapchainIndex     = swapChain->getCurrentBackbufferIndex();

	auto swapchainBuffer      = swapChain->getSwapchainBuffer(swapchainIndex);
	auto swapchainBufferRTV   = swapChain->getSwapchainBufferRTV(swapchainIndex);
	auto commandAllocator     = device->getCommandAllocator(swapchainIndex);
	auto commandList          = device->getCommandList(swapchainIndex);
	auto commandQueue         = device->getCommandQueue();

	// #wip-critical: Call this at the start of frame or end of frame?
	swapChain->swapBackbuffer();

	commandAllocator->reset();
	commandList->reset(commandAllocator);

	{
		SCOPED_DRAW_EVENT(commandList, NullDrawEvent);
		// Real renderer would render something here.
	}

	TextureMemoryBarrier presentBarrier{
		.stateBefore = ETextureMemoryLayout::COMMON,
		.stateAfter  = ETextureMemoryLayout::PRESENT,
		.texture     = swapchainBuffer,
	};

	// #wip-critical: swapchainBuffer is null at startup
	if (swapchainBuffer != nullptr) {
		commandList->resourceBarriers(0, nullptr, 1, &presentBarrier);
	}

	commandList->close();
	commandAllocator->markValid();

	commandQueue->executeCommandList(commandList);

	swapChain->present();

	device->flushCommandQueue();
#endif
}
