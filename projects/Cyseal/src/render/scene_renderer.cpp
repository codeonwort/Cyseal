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
#include "render/vertex_buffer_pool.h"

#define SCENE_UNIFORM_MEMORY_POOL_SIZE (64 * 1024) // 64 KiB

// Should match with common.hlsl
struct SceneUniform
{
	Float4x4 viewMatrix;
	Float4x4 projMatrix;
	Float4x4 viewProjMatrix;

	Float4x4 viewInvMatrix;
	Float4x4 projInvMatrix;
	Float4x4 viewProjInvMatrix;

	vec3 cameraPosition; float _pad0;
	vec3 sunDirection;   float _pad1;
	vec3 sunIlluminance; float _pad2;
};

void SceneRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;
	
	// Scene textures
	{
		const uint32 sceneWidth = renderDevice->getSwapChain()->getBackbufferWidth();
		const uint32 sceneHeight = renderDevice->getSwapChain()->getBackbufferHeight();
		recreateSceneTextures(sceneWidth, sceneHeight);
	}

	// Scene uniforms
	{
		const uint32 swapchainCount = renderDevice->getSwapChain()->getBufferCount();
		CHECK(sizeof(SceneUniform) * swapchainCount <= SCENE_UNIFORM_MEMORY_POOL_SIZE);

		sceneUniformMemory = std::unique_ptr<ConstantBuffer>(
			device->createConstantBuffer(SCENE_UNIFORM_MEMORY_POOL_SIZE));

		{
			DescriptorHeapDesc desc;
			desc.type = EDescriptorHeapType::CBV;
			desc.numDescriptors = swapchainCount;
			desc.flags = EDescriptorHeapFlags::None;
			desc.nodeMask = 0;
			sceneUniformDescriptorHeap = std::unique_ptr<DescriptorHeap>(
				device->createDescriptorHeap(desc));
		}

		sceneUniformCBV = std::unique_ptr<ConstantBufferView>(
			sceneUniformMemory->allocateCBV(
				sceneUniformDescriptorHeap.get(), sizeof(SceneUniform), swapchainCount));
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

	if (accelStructure != nullptr)
	{
		delete accelStructure; accelStructure = nullptr;
	}
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

	updateSceneUniform(backbufferIndex, scene, camera);

	{
		SCOPED_DRAW_EVENT(commandList, GPUScene);

		gpuScene->renderGPUScene(commandList, scene, camera);
	}

	if (scene->bRebuildRaytracingScene)
	{
		SCOPED_DRAW_EVENT(commandList, RaytracingScene);

		rebuildAccelerationStructure(commandList, scene);
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

		basePass->renderBasePass(
			commandList, scene, camera,
			sceneUniformCBV.get(),
			gpuScene->getCulledGPUSceneBuffer());
	}

	// Ray Traced Reflections
	if (device->getRaytracingTier() != ERaytracingTier::NotSupported)
	{
		SCOPED_DRAW_EVENT(commandList, RayTracedReflections);

		// #todo-wip-rt: Renderer
		// Insert resource barriers.
		// Just clear RTR output if DXR is unavailable.
		// Combine sceneColor with RTR.

		rtReflections->renderRayTracedReflections(
			commandList, scene, camera,
			sceneUniformCBV.get(),
			accelStructure,
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

void SceneRenderer::updateSceneUniform(
	uint32 swapchainIndex,
	const SceneProxy* scene,
	const Camera* camera)
{
	SceneUniform uboData;
	uboData.viewMatrix        = camera->getViewMatrix();
	uboData.projMatrix        = camera->getProjMatrix();
	uboData.viewProjMatrix    = camera->getViewProjMatrix();

	uboData.viewInvMatrix     = camera->getViewInvMatrix();
	uboData.projInvMatrix     = camera->getProjInvMatrix();
	uboData.viewProjInvMatrix = camera->getViewProjInvMatrix();

	uboData.cameraPosition    = camera->getPosition();
	uboData.sunDirection      = scene->sun.direction;
	uboData.sunIlluminance    = scene->sun.illuminance;
	
	sceneUniformCBV->upload(&uboData, sizeof(uboData), swapchainIndex);
}

void SceneRenderer::rebuildAccelerationStructure(
	RenderCommandList* commandList,
	const SceneProxy* scene)
{
	// #todo-wip-rt: For now rebuild every BLAS even for a single change.
	// - Entire scene is a TLAS that contains one instance of a BLAS.
	// - That BLAS contains all sections of all StaticMesh.

	const uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();
	const uint32 LOD = 0; // #todo-wip-rt: LOD for BLAS
	std::vector<RaytracingGeometryDesc> geomDescArray;

	// 1. Prepare BLAS transform buffer.
	blasTransformBuffer = std::unique_ptr<StructuredBuffer>(
		device->createStructuredBuffer(numStaticMeshes, 48, EBufferAccessFlags::CPU_WRITE));
	struct Transform3x4
	{
		float m[3][4]; // row-major
	};
	std::vector<Transform3x4> transformData(numStaticMeshes);
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		Float4x4 model = scene->staticMeshes[i]->getTransform().getMatrix(); // row-major
		memcpy(transformData[i].m[0], model.m[0], sizeof(float) * 4);
		memcpy(transformData[i].m[1], model.m[1], sizeof(float) * 4);
		memcpy(transformData[i].m[2], model.m[2], sizeof(float) * 4);
	}
	blasTransformBuffer->uploadData(commandList,
		transformData.data(),
		(uint32)(transformData.size() * sizeof(Transform3x4)),
		0);

	// 2. Prepare BLAS geometry descriptions.
	for (uint32 staticMeshIndex = 0; staticMeshIndex < numStaticMeshes; ++staticMeshIndex)
	{
		StaticMesh* staticMesh = scene->staticMeshes[staticMeshIndex];
		for (const StaticMeshSection& section : staticMesh->getSections(LOD))
		{
			VertexBuffer* vertexBuffer = section.positionBuffer;
			IndexBuffer* indexBuffer = section.indexBuffer;

			RaytracingGeometryDesc geomDesc{};
			geomDesc.type = ERaytracingGeometryType::Triangles;
			geomDesc.triangles.transform3x4Buffer = blasTransformBuffer.get();
			geomDesc.triangles.transformIndex = staticMeshIndex;
			geomDesc.triangles.indexFormat = indexBuffer->getIndexFormat();
			geomDesc.triangles.vertexFormat = EPixelFormat::R32G32B32_FLOAT;
			geomDesc.triangles.indexCount = indexBuffer->getIndexCount();
			geomDesc.triangles.vertexCount = vertexBuffer->getVertexCount();
			geomDesc.triangles.indexBuffer = indexBuffer;
			geomDesc.triangles.vertexBuffer = vertexBuffer;

			// NOTE from Microsoft D3D12RaytracingHelloWorld sample:
			// Mark the geometry as opaque.
			// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
			// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
			geomDesc.flags = ERaytracingGeometryFlags::Opaque;

			geomDescArray.emplace_back(geomDesc);
		}
	}

	// 3. Build acceleration structure.
	accelStructure = commandList->buildRaytracingAccelerationStructure(
		(uint32)geomDescArray.size(), geomDescArray.data());
}
