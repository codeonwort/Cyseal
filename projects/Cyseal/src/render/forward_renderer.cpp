#include "forward_renderer.h"
#include "render/render_command.h"
#include "swap_chain.h"
#include "core/assertion.h"

void ForwardRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;
}

void ForwardRenderer::render(const SceneProxy* scene, const Camera* camera)
{
	auto swapChain = device->getSwapChain();
	auto currentBackBuffer = swapChain->getCurrentBackBuffer();
	auto currentBackBufferRTV = swapChain->getCurrentBackBufferRTV();
	auto defaultDepthStencil = device->getDefaultDepthStencilBuffer();
	auto defaultDSV = device->getDefaultDSV();

	CHECK(currentBackBuffer);
	CHECK(currentBackBufferRTV);
	CHECK(defaultDepthStencil);
	CHECK(defaultDSV);

	auto commandAllocator = device->getCommandAllocator();
	auto commandList = device->getCommandList();
	auto commandQueue = device->getCommandQueue();

	commandAllocator->reset();
	commandList->reset();

	commandList->transitionResource(
		currentBackBuffer,
		EGPUResourceState::PRESENT,
		EGPUResourceState::RENDER_TARGET);

	commandList->transitionResource(
		defaultDepthStencil,
		EGPUResourceState::COMMON,
		EGPUResourceState::DEPTH_WRITE);

	Viewport viewport;
	viewport.topLeftX = 0;
	viewport.topLeftY = 0;
	viewport.width = static_cast<float>(swapChain->getBackBufferWidth());
	viewport.height = static_cast<float>(swapChain->getBackBufferHeight());
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
 	commandList->rsSetViewport(viewport);

	ScissorRect scissorRect;
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = swapChain->getBackBufferWidth();
	scissorRect.bottom = swapChain->getBackBufferHeight();
 	commandList->rsSetScissorRect(scissorRect);

	float clearColor[4] = { 0.5f, 0.0f, 0.0f, 1.0f };
	commandList->clearRenderTargetView(currentBackBufferRTV, clearColor);

	commandList->clearDepthStencilView(
		defaultDSV,
		EClearFlags::DEPTH | EClearFlags::STENCIL,
		1.0f, 0);

	commandList->omSetRenderTarget(currentBackBufferRTV, defaultDSV);

	commandList->transitionResource(
		currentBackBuffer,
		EGPUResourceState::RENDER_TARGET,
		EGPUResourceState::PRESENT);

	commandList->transitionResource(
		defaultDepthStencil,
		EGPUResourceState::DEPTH_WRITE,
		EGPUResourceState::COMMON);

	commandList->close();

	commandQueue->executeCommandList(commandList);

 	swapChain->present();
 	swapChain->swapBackBuffer();

 	device->flushCommandQueue();
}
