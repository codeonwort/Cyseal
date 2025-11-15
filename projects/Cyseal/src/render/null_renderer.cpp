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
	swapChain->prepareBackbuffer();

	uint32 swapchainIndex     = swapChain->getCurrentBackbufferIndex();
	auto swapchainBuffer      = swapChain->getSwapchainBuffer(swapchainIndex);
	auto swapchainBufferRTV   = swapChain->getSwapchainBufferRTV(swapchainIndex);
	auto commandAllocator     = device->getCommandAllocator(swapchainIndex);
	auto commandList          = device->getCommandList(swapchainIndex);
	auto commandQueue         = device->getCommandQueue();

	commandAllocator->reset();
	commandList->reset(commandAllocator);

	commandList->executeCustomCommands();

	TextureBarrierAuto renderToBackbufferBarrier = {
		EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
		swapchainBuffer, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
	};
	commandList->barrierAuto(0, nullptr, 1, &renderToBackbufferBarrier, 0, nullptr);

	{
		SCOPED_DRAW_EVENT(commandList, NullDrawEvent);
		// Real renderer would render something here.
	}

#if VERIFY_DEAR_IMGUI
	{
		commandList->omSetRenderTarget(swapchainBufferRTV, nullptr);

		float clearColor[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
		commandList->beginRenderPass(); // Just for clearRenderTargetView() (it needs to be inside a render pass for Vulkan)
		commandList->clearRenderTargetView(swapchainBufferRTV, clearColor);
		commandList->endRenderPass();

		SCOPED_DRAW_EVENT(commandList, DearImgui);
		DescriptorHeap* imguiHeaps[] = { device->getDearImguiSRVHeap() };
		commandList->setDescriptorHeaps(1, imguiHeaps);

		// - No beginRenderPass() and endRenderPass() as this function handles render pass part internally.
		// - Render pass implicitly converts the swapchain image's layout to PRESENT.
		//   So we pass swapchainBuffer and forcefully change its internal barrier tracker.
		device->renderDearImgui(commandList, swapchainBuffer);
	}
#endif

	TextureBarrierAuto presentBarrier = {
		EBarrierSync::DRAW, EBarrierAccess::COMMON, EBarrierLayout::Present,
		swapchainBuffer, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
	};
	commandList->barrierAuto(0, nullptr, 1, &presentBarrier, 0, nullptr);

	commandList->close();
	commandAllocator->markValid();

	commandQueue->executeCommandList(commandList, swapChain);

	swapChain->present();

	device->flushCommandQueue();

	commandList->executeDeferredDealloc();
#endif
}
