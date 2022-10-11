#include "scene_renderer.h"
#include "core/assertion.h"
#include "render/render_command.h"
#include "render/gpu_resource.h"
#include "render/swap_chain.h"
#include "render/base_pass.h"
#include "render/static_mesh.h"

SceneRenderer::~SceneRenderer()
{
	delete RT_sceneColor;
}

void SceneRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;

	RT_sceneColor = renderDevice->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::R8G8B8A8_UNORM,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV,
			renderDevice->getSwapChain()->getBackbufferWidth(),
			renderDevice->getSwapChain()->getBackbufferHeight(),
			1, 1, 0));

	createRenderPasses();
}

void SceneRenderer::render(const SceneProxy* scene, const Camera* camera)
{
	auto swapChain            = device->getSwapChain();
	uint32 backbufferIndex    = swapChain->getCurrentBackbufferIndex();
	auto currentBackBuffer    = swapChain->getCurrentBackbuffer();
	auto currentBackBufferRTV = swapChain->getCurrentBackbufferRTV();
	auto defaultDepthStencil  = device->getDefaultDepthStencilBuffer();
	auto defaultDSV           = device->getDefaultDSV();
	auto commandAllocator     = device->getCommandAllocator(backbufferIndex);
	auto commandList          = device->getCommandList();
	auto commandQueue         = device->getCommandQueue();

	commandAllocator->reset();
	// #todo-dx12: Is it OK to reset a command list with a different allocator
	// than which was passed to ID3D12Device::CreateCommandList()?
	commandList->reset(commandAllocator);

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

#if 0
	commandList->transitionResource(
		RT_sceneColor,
		EGPUResourceState::COMMON,
		EGPUResourceState::RENDER_TARGET);
	commandList->omSetRenderTarget(RT_sceneColor->getRTV(), defaultDSV);
#else
	commandList->omSetRenderTarget(currentBackBufferRTV, defaultDSV);
#endif

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
