#include "forward_renderer.h"
#include "render/render_command.h"
#include "render/base_pass.h"
#include "render/swap_chain.h"
#include "core/assertion.h"
#include "static_mesh.h"
#include "buffer.h"

void ForwardRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;

	createRenderPasses();
}

void ForwardRenderer::render(const SceneProxy* scene, const Camera* camera)
{
	auto swapChain            = device->getSwapChain();
	auto currentBackBuffer    = swapChain->getCurrentBackBuffer();
	auto currentBackBufferRTV = swapChain->getCurrentBackBufferRTV();
	auto defaultDepthStencil  = device->getDefaultDepthStencilBuffer();
	auto defaultDSV           = device->getDefaultDSV();
	auto commandAllocator     = device->getCommandAllocator();
	auto commandList          = device->getCommandList();
	auto commandQueue         = device->getCommandQueue();

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
		EClearFlags::DEPTH | EClearFlags::STENCIL,
		1.0f, 0);

	//////////////////////////////////////////////////////////////////////////
	// Draw static meshes
	const Matrix viewProjection = camera->getMatrix();
	// #todo: Apply camera transform

	commandList->setPipelineState(basePass->getPipelineState());
	commandList->setGraphicsRootSignature(basePass->getRootSignature());
	commandList->iaSetPrimitiveTopology(basePass->getPrimitiveTopology());
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		// Upload transpose(viewProjection * world)
		//mesh->bind(commandList);
		for (const StaticMeshSection& section : mesh->getSections())
		{
			commandList->iaSetVertexBuffers(0, 1, &section.vertexBuffer);
			commandList->iaSetIndexBuffer(section.indexBuffer);
			commandList->drawIndexedInstanced(section.indexBuffer->getIndexCount(), 1, 0, 0, 0);
		}
	}

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
 	swapChain->swapBackBuffer();

 	device->flushCommandQueue();
}

void ForwardRenderer::createRenderPasses()
{
	basePass = new BasePass;
	basePass->initialize();
}

void ForwardRenderer::destroyRenderPasses()
{
	delete basePass;
}
