#include "null_renderer.h"
#include "rhi/render_command.h"
#include "rhi/swap_chain.h"

#define VERIFY_EMPTY_LOOP 1
#define VERIFY_DEAR_IMGUI 1

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
#if VERIFY_EMPTY_LOOP
	SwapChain* swapChain      = device->getSwapChain();

	uint32 swapchainIndex     = swapChain->getCurrentBackbufferIndex();
	auto swapchainBuffer      = swapChain->getSwapchainBuffer(swapchainIndex);
	auto swapchainBufferRTV   = swapChain->getSwapchainBufferRTV(swapchainIndex);
	auto commandAllocator     = device->getCommandAllocator(swapchainIndex);
	auto commandList          = device->getCommandList(swapchainIndex);
	auto commandQueue         = device->getCommandQueue();

	commandAllocator->reset();
	commandList->reset(commandAllocator);

	TextureMemoryBarrier renderToBackbufferBarrier{
		.stateBefore = ETextureMemoryLayout::PRESENT,
		.stateAfter  = ETextureMemoryLayout::RENDER_TARGET,
		.texture     = swapchainBuffer,
	};
	commandList->resourceBarriers(0, nullptr, 1, &renderToBackbufferBarrier);

	{
		SCOPED_DRAW_EVENT(commandList, NullDrawEvent);
		// Real renderer would render something here.
	}

#if VERIFY_DEAR_IMGUI
	{
		commandList->omSetRenderTarget(swapchainBufferRTV, nullptr);
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(swapchainBufferRTV, clearColor);

		SCOPED_DRAW_EVENT(commandList, DearImgui);
		DescriptorHeap* imguiHeaps[] = { device->getDearImguiSRVHeap() };
		commandList->setDescriptorHeaps(1, imguiHeaps);
		device->renderDearImgui(commandList);
	}
#endif

	TextureMemoryBarrier presentBarrier{
		.stateBefore = ETextureMemoryLayout::RENDER_TARGET,
		.stateAfter  = ETextureMemoryLayout::PRESENT,
		.texture     = swapchainBuffer,
	};
	commandList->resourceBarriers(0, nullptr, 1, &presentBarrier);

	commandList->close();
	commandAllocator->markValid();

	commandQueue->executeCommandList(commandList);

	swapChain->present();
	swapChain->swapBackbuffer();

	device->flushCommandQueue();
#endif
}
