#include "scene_renderer.h"
#include "core/assertion.h"
#include "core/platform.h"
#include "core/plane.h"
#include "rhi/render_command.h"
#include "rhi/gpu_resource.h"
#include "rhi/swap_chain.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/global_descriptor_heaps.h"
#include "render/static_mesh.h"
#include "render/gpu_scene.h"
#include "render/gpu_culling.h"
#include "render/base_pass.h"
#include "render/ray_traced_reflections.h"
#include "render/tone_mapping.h"
#include "render/buffer_visualization.h"
#include "util/profiling.h"

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

	Plane3D cameraFrustum[6];

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

		sceneUniformMemory = std::unique_ptr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = SCENE_UNIFORM_MEMORY_POOL_SIZE,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::CPU_WRITE,
			}
		));

		sceneUniformDescriptorHeap = std::unique_ptr<DescriptorHeap>(device->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV,
				.numDescriptors = swapchainCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
			}
		));

		auto align = [](uint32 size, uint32 alignment) -> uint32
		{
			return (size + (alignment - 1)) & ~(alignment - 1);
		};
		uint32 bufferOffset = 0;
		sceneUniformCBVs.resize(swapchainCount);
		for (uint32 i = 0; i < swapchainCount; ++i)
		{
			sceneUniformCBVs[i] = std::unique_ptr<ConstantBufferView>(
				gRenderDevice->createCBV(
					sceneUniformMemory.get(), 
					sceneUniformDescriptorHeap.get(),
					sizeof(SceneUniform),
					bufferOffset));
			// #todo-rhi: Somehow hide this alignment from rhi level?
			bufferOffset += align(sizeof(SceneUniform), 256);
		}
	}

	// Render passes
	{
		gpuScene = new GPUScene;
		gpuScene->initialize();

		gpuCulling = new GPUCulling;
		gpuCulling->initialize();

		basePass = new BasePass;
		basePass->initialize();

		rtReflections = new RayTracedReflections;
		rtReflections->initialize();

		toneMapping = new ToneMapping;
		toneMapping->initialize();

		bufferVisualization = new BufferVisualization;
		bufferVisualization->initialize();
	}
}

void SceneRenderer::destroy()
{
	delete RT_sceneColor;
	delete RT_sceneDepth;
	delete RT_thinGBufferA;
	delete RT_indirectSpecular;

	delete gpuScene;
	delete gpuCulling;
	delete basePass;
	delete rtReflections;
	delete toneMapping;
	delete bufferVisualization;

	if (accelStructure != nullptr)
	{
		delete accelStructure; accelStructure = nullptr;
	}
}

void SceneRenderer::render(const SceneProxy* scene, const Camera* camera, const RendererOptions& renderOptions)
{
	bool bDoubleBuffering     = device->getCreateParams().bDoubleBuffering;

	auto commandQueue         = device->getCommandQueue();
	auto swapChain            = device->getSwapChain();
	uint32 swapchainIndex     = bDoubleBuffering ? swapChain->getNextBackbufferIndex() : swapChain->getCurrentBackbufferIndex();

	auto swapchainBuffer      = swapChain->getSwapchainBuffer(swapchainIndex);
	auto swapchainBufferRTV   = swapChain->getSwapchainBufferRTV(swapchainIndex);
	auto commandAllocator     = device->getCommandAllocator(swapchainIndex);
	auto commandList          = device->getCommandList(swapchainIndex);

	if (bDoubleBuffering)
	{
		uint32 ix = swapChain->getCurrentBackbufferIndex();
		auto cmdAllocator = device->getCommandAllocator(ix);
		auto cmdList = device->getCommandList(ix);

		if (cmdAllocator->isValid())
		{
			commandQueue->executeCommandList(cmdList);
		}
	}

	// #todo-renderer: Can be different due to resolution scaling
	const uint32 sceneWidth = swapChain->getBackbufferWidth();
	const uint32 sceneHeight = swapChain->getBackbufferHeight();

	const bool bSupportsRaytracing = (device->getRaytracingTier() != ERaytracingTier::NotSupported);

	commandAllocator->reset();
	commandList->reset(commandAllocator);

	// Just execute prior to any standard renderer works.
	// If some custom commands should execute in midst of frame rendering,
	// I need to insert delegates here and there of this SceneRenderer::render() function.
	commandList->executeCustomCommands();

	// #todo-renderer: In future each render pass might write to RTs of different dimensions.
	// Currently all passes work at full resolution.
	commandList->rsSetViewport(Viewport{
		.topLeftX = 0,
		.topLeftY = 0,
		.width    = static_cast<float>(sceneWidth),
		.height   = static_cast<float>(sceneHeight),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	});
	commandList->rsSetScissorRect(ScissorRect{
		.left   = 0,
		.top    = 0,
		.right  = sceneWidth,
		.bottom = sceneHeight,
	});

	updateSceneUniform(commandList, swapchainIndex, scene, camera);

	{
		SCOPED_DRAW_EVENT(commandList, GPUScene);

		gpuScene->renderGPUScene(
			commandList, swapchainIndex,
			scene, camera, sceneUniformCBVs[swapchainIndex].get());
	}

	if (bSupportsRaytracing && scene->bRebuildRaytracingScene)
	{
		SCOPED_DRAW_EVENT(commandList, CreateRaytracingScene);

		// Recreate every BLAS
		rebuildAccelerationStructure(commandList, scene);
	}

	if (bSupportsRaytracing && !scene->bRebuildRaytracingScene)
	{
		SCOPED_DRAW_EVENT(commandList, UpdateRaytracingScene);

		std::vector<BLASInstanceUpdateDesc> updateDescs;
		updateDescs.reserve(scene->staticMeshes.size());
		for (size_t i = 0; i < scene->staticMeshes.size(); ++i)
		{
			StaticMesh* staticMesh = scene->staticMeshes[i];
			Float4x4 modelMatrix = staticMesh->getTransformMatrix(); // row-major

			if (staticMesh->isTransformDirty() == false)
			{
				continue;
			}

			BLASInstanceUpdateDesc desc{};
			memcpy(desc.instanceTransform[0], modelMatrix.m[0], sizeof(float) * 4);
			memcpy(desc.instanceTransform[1], modelMatrix.m[1], sizeof(float) * 4);
			memcpy(desc.instanceTransform[2], modelMatrix.m[2], sizeof(float) * 4);
			desc.blasIndex = (uint32)i;
			
			updateDescs.emplace_back(desc);
		}
		if (updateDescs.size() > 0)
		{
			// Keep all BLAS geometries and only update transforms of BLAS instances.
			// #todo-async-compute: Building accel structure can be moved to async compute pipeline.
			accelStructure->rebuildTLAS(commandList, (uint32)updateDescs.size(), updateDescs.data());
		}
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
			commandList, swapchainIndex,
			scene, camera, renderOptions,
			sceneUniformCBVs[swapchainIndex].get(),
			gpuScene,
			gpuCulling,
			RT_sceneColor, RT_thinGBufferA);
	}

	// Ray Traced Reflections
	bool bRenderRTR = bSupportsRaytracing && renderOptions.bEnableRayTracedReflections;
	if (!bRenderRTR)
	{
		SCOPED_DRAW_EVENT(commandList, ClearRayTracedReflections);

		ResourceBarrier barriersBefore[] = {
			{
				EResourceBarrierType::Transition,
				RT_indirectSpecular,
				EGPUResourceState::PIXEL_SHADER_RESOURCE,
				EGPUResourceState::RENDER_TARGET
			}
		};
		commandList->resourceBarriers(_countof(barriersBefore), barriersBefore);

		// Clear RTR as a render target, every frame. (not so ideal but works)
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(RT_indirectSpecular->getRTV(), clearColor);

		ResourceBarrier barriersAfter[] = {
			{
				EResourceBarrierType::Transition,
				RT_indirectSpecular,
				EGPUResourceState::RENDER_TARGET,
				EGPUResourceState::UNORDERED_ACCESS
			}
		};
		commandList->resourceBarriers(_countof(barriersAfter), barriersAfter);
	}
	else
	{
		SCOPED_DRAW_EVENT(commandList, RayTracedReflections);

		ResourceBarrier barriers[] = {
			{
				EResourceBarrierType::Transition,
				RT_indirectSpecular,
				EGPUResourceState::PIXEL_SHADER_RESOURCE,
				EGPUResourceState::UNORDERED_ACCESS
			}
		};
		commandList->resourceBarriers(_countof(barriers), barriers);

		rtReflections->renderRayTracedReflections(
			commandList, swapchainIndex, scene, camera,
			sceneUniformCBVs[swapchainIndex].get(),
			accelStructure,
			gpuScene,
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
				RT_indirectSpecular,
				EGPUResourceState::UNORDERED_ACCESS,
				EGPUResourceState::PIXEL_SHADER_RESOURCE
			},
			{
				EResourceBarrierType::Transition,
				swapchainBuffer,
				EGPUResourceState::PRESENT,
				EGPUResourceState::RENDER_TARGET
			}
		};
		commandList->resourceBarriers(_countof(barriers), barriers);

		// #todo-renderer: Should not be here
		commandList->omSetRenderTarget(swapchainBufferRTV, nullptr);

		toneMapping->renderToneMapping(
			commandList,
			swapchainIndex,
			RT_sceneColor,
			RT_indirectSpecular);
	}

	// Buffer visualization
	// final target: back buffer
	if (renderOptions.bufferVisualization != EBufferVisualizationMode::None)
	{
		SCOPED_DRAW_EVENT(commandList, BufferVisualization);

		BufferVisualizationSources sources{
			.mode             = renderOptions.bufferVisualization,
			.sceneColor       = RT_sceneColor,
			.indirectSpecular = RT_indirectSpecular,
		};

		bufferVisualization->renderVisualization(
			commandList,
			swapchainIndex,
			sources);
	}

	//////////////////////////////////////////////////////////////////////////
	// Dear Imgui: Record commands

	{
		SCOPED_DRAW_EVENT(commandList, DearImgui);

		DescriptorHeap* imguiHeaps[] = { device->getDearImguiSRVHeap() };
		commandList->setDescriptorHeaps(1, imguiHeaps);
		device->renderDearImgui(commandList);
	}

	//////////////////////////////////////////////////////////////////////////
	// Finalize

	ResourceBarrier presentBarrier{
		EResourceBarrierType::Transition,
		swapchainBuffer,
		EGPUResourceState::RENDER_TARGET,
		EGPUResourceState::PRESENT
	};
	commandList->resourceBarriers(1, &presentBarrier);

	commandList->close();
	commandAllocator->markValid();

	if (!bDoubleBuffering)
	{
		commandQueue->executeCommandList(commandList);
	}

	swapChain->present();
	swapChain->swapBackbuffer();

	{
		SCOPED_CPU_EVENT(WaitForGPU);

		device->flushCommandQueue();
	}

	commandList->executeDeferredDealloc();

	const_cast<SceneProxy*>(scene)->tempCleanupOriginalScene();
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
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			sceneWidth, sceneHeight,
			1, 1, 0));
	RT_indirectSpecular->setDebugName(L"IndirectSpecular");
}

void SceneRenderer::updateSceneUniform(
	RenderCommandList* commandList,
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

	camera->getFrustum(uboData.cameraFrustum);

	uboData.cameraPosition    = camera->getPosition();
	uboData.sunDirection      = scene->sun.direction;
	uboData.sunIlluminance    = scene->sun.illuminance;
	
	sceneUniformCBVs[swapchainIndex]->writeToGPU(commandList, &uboData, sizeof(uboData));
}

void SceneRenderer::rebuildAccelerationStructure(
	RenderCommandList* commandList,
	const SceneProxy* scene)
{
	// - Entire scene is a TLAS that contains a list of BLAS instances.
	// - Each BLAS contains all sections of each StaticMesh.

	const uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();
	const uint32 LOD = 0; // #todo-lod: LOD for BLAS geometries

	// Prepare BLAS instances.
	std::vector<BLASInstanceInitDesc> blasDescArray(numStaticMeshes);
	for (uint32 staticMeshIndex = 0; staticMeshIndex < numStaticMeshes; ++staticMeshIndex)
	{
		StaticMesh* staticMesh = scene->staticMeshes[staticMeshIndex];
		BLASInstanceInitDesc& blasDesc = blasDescArray[staticMeshIndex];

		Float4x4 modelMatrix = staticMesh->getTransformMatrix(); // row-major
		memcpy(blasDesc.instanceTransform[0], modelMatrix.m[0], sizeof(float) * 4);
		memcpy(blasDesc.instanceTransform[1], modelMatrix.m[1], sizeof(float) * 4);
		memcpy(blasDesc.instanceTransform[2], modelMatrix.m[2], sizeof(float) * 4);

		for (const StaticMeshSection& section : staticMesh->getSections(LOD))
		{
			VertexBuffer* vertexBuffer = section.positionBuffer->getGPUResource().get();
			IndexBuffer* indexBuffer = section.indexBuffer->getGPUResource().get();

			RaytracingGeometryDesc geomDesc{};
			geomDesc.type = ERaytracingGeometryType::Triangles;
			// modelMatrix is applied as BLAS instance transform, not as geometry transform.
			//geomDesc.triangles.transform3x4Buffer = blasTransformBuffer.get();
			//geomDesc.triangles.transformIndex = staticMeshIndex;
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

			blasDesc.geomDescs.emplace_back(geomDesc);
		}
	}

	// Build acceleration structure.
	accelStructure = commandList->buildRaytracingAccelerationStructure(
		(uint32)blasDescArray.size(), blasDescArray.data());
}
