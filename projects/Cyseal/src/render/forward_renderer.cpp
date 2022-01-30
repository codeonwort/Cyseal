#include "forward_renderer.h"
#include "core/assertion.h"
#include "render/render_command.h"
#include "render/gpu_resource.h"
#include "render/swap_chain.h"
#include "render/base_pass.h"
#include "render/static_mesh.h"

void ForwardRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;

	createRenderPasses();
}

void ForwardRenderer::render(const SceneProxy* scene, const Camera* camera)
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

	//////////////////////////////////////////////////////////////////////////
	// #todo: Depth PrePass

	//////////////////////////////////////////////////////////////////////////
	// BasePass
	
	// Draw static meshes
	const Matrix viewProjection = camera->getMatrix();

	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature
	//   Setting a PSO does not change the root signature.
	//   The application must call a dedicated API for setting the root signature.
	commandList->setPipelineState(basePass->getPipelineState());
	commandList->setGraphicsRootSignature(basePass->getRootSignature());

	commandList->iaSetPrimitiveTopology(basePass->getPrimitiveTopology());

	basePass->bindRootParameters(commandList, (uint32)scene->staticMeshes.size());

	uint32 payloadID = 0;
	for (const StaticMesh* mesh : scene->staticMeshes)
	{
		// todo-wip: constant buffer
		const Matrix model = mesh->getTransform().getMatrix();
		const Matrix MVP = model * viewProjection;
		Material* material = mesh->getMaterial();

		BasePass::ConstantBufferPayload payload;
		payload.mvpTransform = MVP;
		payload.r = 0.0f;
		payload.g = 1.0f;
		payload.b = 0.0f;
		payload.a = 1.0f;

		basePass->updateConstantBuffer(payloadID, &payload, sizeof(payload));
		basePass->updateMaterial(commandList, payloadID, material);

		// rootParameterIndex, constant, destOffsetIn32BitValues
		commandList->setGraphicsRootConstant32(0, payloadID, 0);

		for (const StaticMeshSection& section : mesh->getSections())
		{
			commandList->iaSetVertexBuffers(0, 1, &section.vertexBuffer);
			commandList->iaSetIndexBuffer(section.indexBuffer);
			commandList->drawIndexedInstanced(section.indexBuffer->getIndexCount(), 1, 0, 0, 0);
		}

		++payloadID;
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
 	swapChain->swapBackbuffer();

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
