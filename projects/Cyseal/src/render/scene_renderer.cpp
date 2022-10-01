#include "scene_renderer.h"
#include "core/assertion.h"
#include "render/render_command.h"
#include "render/gpu_resource.h"
#include "render/swap_chain.h"
#include "render/base_pass.h"
#include "render/static_mesh.h"

void SceneRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;

	createRenderPasses();
}

void SceneRenderer::render(const SceneProxy* scene, const Camera* camera)
{
	auto swapChain            = device->getSwapChain();
	auto currentBackBuffer    = swapChain->getCurrentBackbuffer();
	auto currentBackBufferRTV = swapChain->getCurrentBackbufferRTV();
	auto defaultDepthStencil  = device->getDefaultDepthStencilBuffer();
	auto defaultDSV           = device->getDefaultDSV();
	auto commandAllocator     = device->getCommandAllocator();
	auto commandList          = device->getCommandList();
	auto commandQueue         = device->getCommandQueue();

	commandAllocator->reset();
	commandList->reset();

	commandList->executeCustomCommands();

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
	viewport.width    = static_cast<float>(swapChain->getBackbufferWidth());
	viewport.height   = static_cast<float>(swapChain->getBackbufferHeight());
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
 	commandList->rsSetViewport(viewport);

	ScissorRect scissorRect;
	scissorRect.left   = 0;
	scissorRect.top    = 0;
	scissorRect.right  = swapChain->getBackbufferWidth();
	scissorRect.bottom = swapChain->getBackbufferHeight();
 	commandList->rsSetScissorRect(scissorRect);

	commandList->omSetRenderTarget(currentBackBufferRTV, defaultDSV);

	float clearColor[4] = { 0.5f, 0.0f, 0.0f, 1.0f };
	commandList->clearRenderTargetView(currentBackBufferRTV, clearColor);

	commandList->clearDepthStencilView(
		defaultDSV,
		EDepthClearFlags::DEPTH_STENCIL,
		1.0f, 0);

	// #todo: Depth PrePass

	basePass->renderBasePass(commandList, scene, camera);

	//////////////////////////////////////////////////////////////////////////
	// Present
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
 	swapChain->swapBackbuffer();

 	device->flushCommandQueue();
}

void SceneRenderer::createRenderPasses()
{
	basePass = new BasePass;
	basePass->initialize();
}

void SceneRenderer::destroyRenderPasses()
{
	delete basePass;
}
