#include "scene_renderer.h"
#include "core/assertion.h"
#include "render/render_command.h"
#include "render/gpu_resource.h"
#include "render/swap_chain.h"
#include "render/static_mesh.h"
#include "render/gpu_scene.h"
#include "render/base_pass.h"
#include "render/ray_traced_reflections.h"
#include "render/tone_mapping.h"

void SceneRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;

	// Scene textures
	{
		const uint32 sceneWidth = renderDevice->getSwapChain()->getBackbufferWidth();
		const uint32 sceneHeight = renderDevice->getSwapChain()->getBackbufferHeight();
		recreateSceneTextures(sceneWidth, sceneHeight);
	}

	// Render passes
	{
		gpuScene = new GPUScene;
		gpuScene->initialize();

		basePass = new BasePass;
		basePass->initialize();

		rtReflections = new RayTracedReflections;
		rtReflections->initialize();

		toneMapping = new ToneMapping;
	}
}

void SceneRenderer::destroy()
{
	delete RT_sceneColor;
	delete RT_sceneDepth;
	delete RT_thinGBufferA;
	delete RT_indirectSpecular;

	delete gpuScene;
	delete basePass;
	delete rtReflections;
	delete toneMapping;
}

void SceneRenderer::render(const SceneProxy* scene, const Camera* camera)
{
	auto swapChain            = device->getSwapChain();
	uint32 backbufferIndex    = swapChain->getCurrentBackbufferIndex();
	auto currentBackBuffer    = swapChain->getCurrentBackbuffer();
	auto currentBackBufferRTV = swapChain->getCurrentBackbufferRTV();
	auto commandAllocator     = device->getCommandAllocator(backbufferIndex);
	auto commandList          = device->getCommandList();
	auto commandQueue         = device->getCommandQueue();

	// #todo-renderer: Can be different due to resolution scaling
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

	{
		SCOPED_DRAW_EVENT(commandList, GPUScene);

		gpuScene->renderGPUScene(commandList, scene, camera);
	}

	// #todo-renderer: Depth PrePass
	{
	}

	// Base pass
	{
		SCOPED_DRAW_EVENT(commandList, BasePass);

		ResourceBarrier barriers[] = {
			{
				EResourceBarrierType::Transition,
				RT_sceneColor,
				EGPUResourceState::PIXEL_SHADER_RESOURCE,
				EGPUResourceState::RENDER_TARGET
			},
			{
				EResourceBarrierType::Transition,
				RT_thinGBufferA,
				EGPUResourceState::PIXEL_SHADER_RESOURCE,
				EGPUResourceState::RENDER_TARGET
			}
		};
		commandList->resourceBarriers(_countof(barriers), barriers);

		RenderTargetView* RTVs[] = { RT_sceneColor->getRTV(), RT_thinGBufferA->getRTV() };
		commandList->omSetRenderTargets(_countof(RTVs), RTVs, RT_sceneDepth->getDSV());

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(RT_sceneColor->getRTV(), clearColor);
		commandList->clearRenderTargetView(RT_thinGBufferA->getRTV(), clearColor);
		commandList->clearDepthStencilView(RT_sceneDepth->getDSV(), EDepthClearFlags::DEPTH_STENCIL, 1.0f, 0);

		basePass->renderBasePass(commandList, scene, camera, gpuScene->getCulledGPUSceneBuffer());
	}

	// Ray Traced Reflections
	if (device->getRaytracingTier() != ERaytracingTier::NotSupported)
	{
		// #todo-wip-rt: Renderer
		// Insert resource barriers.
		// Just clear RTR output if DXR is unavailable.
		// Combine sceneColor with RTR.

		rtReflections->renderRayTracedReflections(
			commandList, scene, camera,
			RT_thinGBufferA, RT_indirectSpecular,
			sceneWidth, sceneHeight);
	}

	// Tone mapping
	// final target: back buffer
	{
		SCOPED_DRAW_EVENT(commandList, ToneMapping);

		ResourceBarrier barriers[] = {
			{
				EResourceBarrierType::Transition,
				RT_sceneColor,
				EGPUResourceState::RENDER_TARGET,
				EGPUResourceState::PIXEL_SHADER_RESOURCE
			},
			{
				EResourceBarrierType::Transition,
				RT_thinGBufferA,
				EGPUResourceState::RENDER_TARGET,
				EGPUResourceState::PIXEL_SHADER_RESOURCE
			},
			{
				EResourceBarrierType::Transition,
				currentBackBuffer,
				EGPUResourceState::PRESENT,
				EGPUResourceState::RENDER_TARGET
			}
		};
		commandList->resourceBarriers(_countof(barriers), barriers);

		// #todo-renderer: Should not be here
		commandList->omSetRenderTarget(currentBackBufferRTV, nullptr);

		toneMapping->renderToneMapping(commandList, RT_sceneColor);
	}

	//////////////////////////////////////////////////////////////////////////
	// Finalize

	ResourceBarrier presentBarrier{
		EResourceBarrierType::Transition,
		currentBackBuffer,
		EGPUResourceState::RENDER_TARGET,
		EGPUResourceState::PRESENT
	};
	commandList->resourceBarriers(1, &presentBarrier);

	commandList->close();

	commandQueue->executeCommandList(commandList);

	swapChain->present();
	swapChain->swapBackbuffer();

 	device->flushCommandQueue();
}

void SceneRenderer::recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight)
{
	RT_sceneColor = device->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::R32G32B32A32_FLOAT,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV,
			sceneWidth, sceneHeight,
			1, 1, 0));
	RT_sceneColor->setDebugName(L"SceneColor");

	RT_sceneDepth = device->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::D24_UNORM_S8_UINT,
			ETextureAccessFlags::DSV,
			sceneWidth, sceneHeight,
			1, 1, 0));
	RT_sceneDepth->setDebugName(L"SceneDepth");

	RT_thinGBufferA = device->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::R16G16B16A16_FLOAT,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			sceneWidth, sceneHeight,
			1, 1, 0));
	RT_thinGBufferA->setDebugName(L"ThinGBufferA");

	RT_indirectSpecular = device->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::R16G16B16A16_FLOAT,
			ETextureAccessFlags::UAV | ETextureAccessFlags::SRV,
			sceneWidth, sceneHeight,
			1, 1, 0));
	RT_indirectSpecular->setDebugName(L"IndirectSpecular");
}
