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
				EPixelFormat::R32G32B32A32_FLOAT,
				ETextureAccessFlags::RTV | ETextureAccessFlags::SRV,
				sceneWidth, sceneHeight,
				1, 1, 0));
		RT_sceneColor->setDebugName(L"SceneColor");

		RT_sceneDepth = renderDevice->createTexture(
			TextureCreateParams::texture2D(
				EPixelFormat::D24_UNORM_S8_UINT,
				ETextureAccessFlags::DSV,
				sceneWidth, sceneHeight,
				1, 1, 0));
		RT_sceneDepth->setDebugName(L"SceneDepth");
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

	const uint32 sceneWidth = swapChain->getBackbufferWidth();
	const uint32 sceneHeight = swapChain->getBackbufferHeight();

	commandAllocator->reset();
	// #todo-dx12: Is it OK to reset a command list with a different allocator
	// than which was passed to ID3D12Device::CreateCommandList()?
	commandList->reset(commandAllocator);

	commandList->executeCustomCommands();

	Viewport viewport;
	viewport.topLeftX = 0;
	viewport.topLeftY = 0;
	viewport.width    = static_cast<float>(sceneWidth);
	viewport.height   = static_cast<float>(sceneHeight);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
 	commandList->rsSetViewport(viewport);

	ScissorRect scissorRect;
	scissorRect.left   = 0;
	scissorRect.top    = 0;
	scissorRect.right  = sceneWidth;
	scissorRect.bottom = sceneHeight;
 	commandList->rsSetScissorRect(scissorRect);

	// #todo: Depth PrePass

	// Base pass
	// final target: RT_sceneColor, RT_sceneDepth
	{
		commandList->transitionResource(
			RT_sceneColor,
			EGPUResourceState::PIXEL_SHADER_RESOURCE,
			EGPUResourceState::RENDER_TARGET);

		commandList->omSetRenderTarget(RT_sceneColor->getRTV(), RT_sceneDepth->getDSV());

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(RT_sceneColor->getRTV(), clearColor);

		commandList->clearDepthStencilView(
			RT_sceneDepth->getDSV(),
			EDepthClearFlags::DEPTH_STENCIL,
			1.0f, 0);

		basePass->renderBasePass(commandList, scene, camera);
	}

	// Tone mapping
	// final target: back buffer
	{
		commandList->transitionResource(
			RT_sceneColor,
			EGPUResourceState::RENDER_TARGET,
			EGPUResourceState::PIXEL_SHADER_RESOURCE);

		commandList->transitionResource(
			currentBackBuffer,
			EGPUResourceState::PRESENT,
			EGPUResourceState::RENDER_TARGET);

		commandList->transitionResource(
			backbufferDepth,
			EGPUResourceState::COMMON,
			EGPUResourceState::DEPTH_WRITE);

		commandList->omSetRenderTarget(currentBackBufferRTV, backbufferDSV);

		toneMapping->renderToneMapping(commandList, RT_sceneColor);
	}

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
