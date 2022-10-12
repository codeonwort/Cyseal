#include "scene_renderer.h"
#include "core/assertion.h"
#include "render/render_command.h"
#include "render/gpu_resource.h"
#include "render/swap_chain.h"
#include "render/static_mesh.h"
#include "render/base_pass.h"
#include "render/tone_mapping.h"

void SceneRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;

	// Scene textures
	{
		const uint32 sceneWidth = renderDevice->getSwapChain()->getBackbufferWidth();
		const uint32 sceneHeight = renderDevice->getSwapChain()->getBackbufferHeight();

		RT_sceneColor = renderDevice->createTexture(
			TextureCreateParams::texture2D(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::RTV | ETextureAccessFlags::SRV,
				sceneWidth, sceneHeight,
				1, 1, 0));

		RT_sceneDepth = renderDevice->createTexture(
			TextureCreateParams::texture2D(
				EPixelFormat::D24_UNORM_S8_UINT,
				ETextureAccessFlags::DSV,
				sceneWidth, sceneHeight,
				1, 1, 0));
	}

	// Render passes
	{
		basePass = new BasePass;
		basePass->initialize();

		toneMapping = new ToneMapping;
	}
}

void SceneRenderer::destroy()
{
	delete RT_sceneColor;
	delete RT_sceneDepth;

	delete basePass;
	delete toneMapping;
}

void SceneRenderer::render(const SceneProxy* scene, const Camera* camera)
{
	auto swapChain            = device->getSwapChain();
	uint32 backbufferIndex    = swapChain->getCurrentBackbufferIndex();
	auto currentBackBuffer    = swapChain->getCurrentBackbuffer();
	auto currentBackBufferRTV = swapChain->getCurrentBackbufferRTV();
	auto backbufferDepth      = device->getDefaultDepthStencilBuffer();
	auto backbufferDSV        = device->getDefaultDSV();
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
		backbufferDepth,
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
	commandList->omSetRenderTarget(currentBackBufferRTV, backbufferDSV);
#endif

	float clearColor[4] = { 0.5f, 0.0f, 0.0f, 1.0f };
	commandList->clearRenderTargetView(currentBackBufferRTV, clearColor);

	commandList->clearDepthStencilView(
		backbufferDSV,
		EDepthClearFlags::DEPTH_STENCIL,
		1.0f, 0);

	// #todo: Depth PrePass

	basePass->renderBasePass(commandList, scene, camera);

	toneMapping->renderToneMapping(commandList, RT_sceneColor);

	//////////////////////////////////////////////////////////////////////////
	// Present
	commandList->transitionResource(
		currentBackBuffer,
		EGPUResourceState::RENDER_TARGET,
		EGPUResourceState::PRESENT);

	commandList->transitionResource(
		backbufferDepth,
		EGPUResourceState::DEPTH_WRITE,
		EGPUResourceState::COMMON);

	commandList->close();

	commandQueue->executeCommandList(commandList);

 	swapChain->present();
 	swapChain->swapBackbuffer();

 	device->flushCommandQueue();
}
