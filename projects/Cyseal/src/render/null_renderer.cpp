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

void NullRenderer::render(const SceneProxy* scene, const Camera* camera)
{
#if EMPTY_LOOP
	auto swapChain = device->getSwapChain();
	uint32 swapchainIndex = swapChain->getCurrentBackbufferIndex();
	auto currentBackBuffer = swapChain->getCurrentBackbuffer();
	auto currentBackBufferRTV = swapChain->getCurrentBackbufferRTV();
	auto commandAllocator = device->getCommandAllocator(swapchainIndex);
	auto commandList = device->getCommandList(swapchainIndex);
	auto commandQueue = device->getCommandQueue();

	commandAllocator->reset();

	commandList->reset(commandAllocator);

	{
		// Real renderer would render something here.
	}

	commandList->close();

	commandQueue->executeCommandList(commandList);

	swapChain->present();
	swapChain->swapBackbuffer();

	device->flushCommandQueue();
#endif
}
