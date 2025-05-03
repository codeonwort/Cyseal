#include "scene_renderer.h"

#include "core/assertion.h"
#include "core/platform.h"
#include "core/plane.h"

#include "rhi/rhi_policy.h"
#include "rhi/render_command.h"
#include "rhi/gpu_resource.h"
#include "rhi/swap_chain.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/global_descriptor_heaps.h"
#include "rhi/texture_manager.h"
#include "rhi/hardware_raytracing.h"
#include "rhi/denoiser_device.h"

#include "render/static_mesh.h"
#include "render/gpu_scene.h"
#include "render/gpu_culling.h"
#include "render/bilateral_blur.h"
#include "render/base_pass.h"
#include "render/sky_pass.h"
#include "render/tone_mapping.h"
#include "render/buffer_visualization.h"
#include "render/raytracing/ray_traced_shadows.h"
#include "render/raytracing/indirect_diffuse_pass.h"
#include "render/raytracing/indirect_specular_pass.h"
#include "render/pathtracing/path_tracing_pass.h"
#include "render/pathtracing/denoiser_plugin_pass.h"

#include "util/profiling.h"

#define SCENE_UNIFORM_MEMORY_POOL_SIZE (64 * 1024) // 64 KiB

static const EPixelFormat PF_sceneColor = EPixelFormat::R32G32B32A32_FLOAT;
static const EPixelFormat PF_velocityMap = EPixelFormat::R16G16_FLOAT;
static const EPixelFormat PF_gbuffers[SceneRenderer::NUM_GBUFFERS] = {
	EPixelFormat::R32G32B32A32_UINT, //EPixelFormat::R16G16B16A16_FLOAT,
	EPixelFormat::R16G16B16A16_FLOAT,
};

// https://github.com/microsoft/DirectX-Specs/blob/master/d3d/PlanarDepthStencilDDISpec.md
// NOTE: Also need to change backbufferDepthFormat in render_device.h
#if 0
	// Depth 24-bit, Stencil 8-bit
static const EPixelFormat DEPTH_TEXTURE_FORMAT = EPixelFormat::R24G8_TYPELESS;
static const EPixelFormat DEPTH_DSV_FORMAT = EPixelFormat::D24_UNORM_S8_UINT;
static const EPixelFormat DEPTH_SRV_FORMAT = EPixelFormat::R24_UNORM_X8_TYPELESS;
#else
	// Depth 32-bit, Stencil 8-bit
static const EPixelFormat DEPTH_TEXTURE_FORMAT = EPixelFormat::R32G8X24_TYPELESS;
static const EPixelFormat DEPTH_DSV_FORMAT = EPixelFormat::D32_FLOAT_S8_UINT;
static const EPixelFormat DEPTH_SRV_FORMAT = EPixelFormat::R32_FLOAT_X8X24_TYPELESS;
#endif

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

		sceneUniformMemory = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = SCENE_UNIFORM_MEMORY_POOL_SIZE,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC | EBufferAccessFlags::CBV,
			}
		));

		sceneUniformDescriptorHeap = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV,
				.numDescriptors = swapchainCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
			}
		));

		uint32 bufferOffset = 0;
		sceneUniformCBVs.initialize(swapchainCount);
		for (uint32 i = 0; i < swapchainCount; ++i)
		{
			sceneUniformCBVs[i] = UniquePtr<ConstantBufferView>(
				gRenderDevice->createCBV(
					sceneUniformMemory.get(),
					sceneUniformDescriptorHeap.get(),
					sizeof(SceneUniform),
					bufferOffset));

			uint32 alignment = gRenderDevice->getConstantBufferDataAlignment();
			bufferOffset += Cymath::alignBytes(sizeof(SceneUniform), alignment);
		}
	}

	// Render passes
	{
		sceneRenderPasses.push_back(gpuScene = new GPUScene);
		sceneRenderPasses.push_back(gpuCulling = new GPUCulling);
		sceneRenderPasses.push_back(bilateralBlur = new BilateralBlur);
		sceneRenderPasses.push_back(rayTracedShadowsPass = new RayTracedShadowsPass);
		sceneRenderPasses.push_back(basePass = new BasePass);
		sceneRenderPasses.push_back(skyPass = new SkyPass);
		sceneRenderPasses.push_back(indirectDiffusePass = new IndirectDiffusePass);
		sceneRenderPasses.push_back(indirectSpecularPass = new IndirecSpecularPass);
		sceneRenderPasses.push_back(toneMapping = new ToneMapping);
		sceneRenderPasses.push_back(bufferVisualization = new BufferVisualization);
		sceneRenderPasses.push_back(pathTracingPass = new PathTracingPass);
		sceneRenderPasses.push_back(denoiserPluginPass = new DenoiserPluginPass);

		gpuScene->initialize();
		gpuCulling->initialize(kMaxBasePassPermutation);
		bilateralBlur->initialize();
		rayTracedShadowsPass->initialize();
		basePass->initialize(PF_sceneColor, PF_gbuffers, NUM_GBUFFERS, PF_velocityMap);
		skyPass->initialize(PF_sceneColor);
		indirectDiffusePass->initialize();
		indirectSpecularPass->initialize();
		toneMapping->initialize();
		bufferVisualization->initialize();
		pathTracingPass->initialize();
		denoiserPluginPass->initialize();
	}
}

void SceneRenderer::destroy()
{
	RT_sceneColor.reset();
	RT_sceneDepth.reset();
	RT_prevSceneDepth.reset();
	for (uint32 i=0; i<NUM_GBUFFERS; ++i) RT_gbuffers[i].reset();
	RT_shadowMask.reset();
	RT_indirectDiffuse.reset();
	RT_indirectSpecular.reset();
	RT_pathTracing.reset();

	accelStructure.reset();

	for (auto pass : sceneRenderPasses) delete pass;
	sceneRenderPasses.clear();
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
	const bool bRenderPathTracing = bSupportsRaytracing && (renderOptions.pathTracing != EPathTracingMode::Disabled);
	
	const bool bRenderRayTracedShadows = bSupportsRaytracing
		&& renderOptions.rayTracedShadows != ERayTracedShadowsMode::Disabled
		&& bRenderPathTracing == false;

	// If disabled, RT_indirectDiffuse will be cleared as black
	// so that tone mapping pass reads indirectDiffuse as zero.
	const bool bRenderIndirectDiffuse = bSupportsRaytracing
		&& renderOptions.indirectDiffuse != EIndirectDiffuseMode::Disabled
		&& bRenderPathTracing == false;

	// If disabled, RT_indirectSpecular will be cleared as black
	// so that tone mapping pass reads indirectSpecular as zero.
	const bool bRenderIndirectSpecular = bSupportsRaytracing
		&& renderOptions.indirectSpecular != EIndirectSpecularMode::Disabled
		&& bRenderPathTracing == false;

	const bool bRenderAnyRaytracingPass = renderOptions.anyRayTracingEnabled();

	rebuildFrameResources(commandList, scene);

	resetCommandList(commandAllocator, commandList);

	// Just execute prior to any standard renderer works.
	// If some custom commands should execute in midst of frame rendering,
	// I need to insert delegates here and there of this SceneRenderer::render() function.
	commandList->executeCustomCommands();

	// #todo-renderer: In future each render pass might write to RTs of different dimensions.
	// Currently all passes work at full resolution.
	const Viewport fullscreenViewport{
		.topLeftX = 0,
		.topLeftY = 0,
		.width    = static_cast<float>(sceneWidth),
		.height   = static_cast<float>(sceneHeight),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	const ScissorRect fullscreenScissorRect{
		.left   = 0,
		.top    = 0,
		.right  = sceneWidth,
		.bottom = sceneHeight,
	};
	commandList->rsSetViewport(fullscreenViewport);
	commandList->rsSetScissorRect(fullscreenScissorRect);

	updateSceneUniform(commandList, swapchainIndex, scene, camera);
	auto sceneUniformCBV = sceneUniformCBVs.at(swapchainIndex);

	{
		SCOPED_DRAW_EVENT(commandList, GPUScene);

		GPUSceneInput passInput{
			.scene                    = scene,
			.camera                   = camera,
			.sceneUniform             = sceneUniformCBV,
			.bRenderAnyRaytracingPass = bRenderAnyRaytracingPass,
		};
		gpuScene->renderGPUScene(commandList, swapchainIndex, passInput);
	}

	if (bSupportsRaytracing && scene->bRebuildRaytracingScene)
	{
		SCOPED_DRAW_EVENT(commandList, CreateRaytracingScene);

		// Recreate every BLAS
		rebuildAccelerationStructure(commandList, scene);

		GPUResource* uavBarrierResources[] = { accelStructure.get() };
		commandList->resourceBarriers(0, nullptr, 0, nullptr, 1, uavBarrierResources);
	}

	if (bSupportsRaytracing && !scene->bRebuildRaytracingScene)
	{
		SCOPED_DRAW_EVENT(commandList, UpdateRaytracingScene);

		std::vector<BLASInstanceUpdateDesc> updateDescs;
		updateDescs.reserve(scene->staticMeshes.size());
		for (size_t i = 0; i < scene->staticMeshes.size(); ++i)
		{
			StaticMeshProxy* staticMesh = scene->staticMeshes[i];
			Float4x4 modelMatrix = staticMesh->getLocalToWorld(); // row-major

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

		GPUResource* uavBarrierResources[] = { accelStructure.get() };
		commandList->resourceBarriers(0, nullptr, 0, nullptr, 1, uavBarrierResources);
	}

	// #todo-renderer: Depth PrePass
	{
	}

	// Ray Traced Shadows
	if (!bRenderRayTracedShadows)
	{
		SCOPED_DRAW_EVENT(commandList, ClearRayTracedShadows);

		TextureMemoryBarrier barriersBefore[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::RENDER_TARGET, RT_shadowMask.get() }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersBefore), barriersBefore);

		// Clear as a render target. (not so ideal but works)
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		commandList->clearRenderTargetView(shadowMaskRTV.get(), clearColor);

		TextureMemoryBarrier barriersAfter[] = {
			{ ETextureMemoryLayout::RENDER_TARGET, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_shadowMask.get() }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersAfter), barriersAfter);
	}
	else
	{
		SCOPED_DRAW_EVENT(commandList, RayTracedShadows);

		TextureMemoryBarrier barriersBefore[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::UNORDERED_ACCESS, RT_shadowMask.get(), }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersBefore), barriersBefore);

		RayTracedShadowsInput passInput{
			.scene              = scene,
			.camera             = camera,
			.mode               = renderOptions.rayTracedShadows,
			.sceneWidth         = sceneWidth,
			.sceneHeight        = sceneHeight,
			.sceneUniformBuffer = sceneUniformCBV,
			.gpuScene           = gpuScene,
			.raytracingScene    = accelStructure.get(),
			.shadowMaskUAV      = shadowMaskUAV.get(),
		};
		rayTracedShadowsPass->renderRayTracedShadows(commandList, swapchainIndex, passInput);

		TextureMemoryBarrier barriersAfter[] = {
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_shadowMask.get(), }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersAfter), barriersAfter);
	}

	// Base pass
	{
		SCOPED_DRAW_EVENT(commandList, BasePass);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::RENDER_TARGET, RT_sceneColor.get() },
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::RENDER_TARGET, RT_gbuffers[0].get() },
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::RENDER_TARGET, RT_gbuffers[1].get() },
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::RENDER_TARGET, RT_velocityMap.get() },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);

		RenderTargetView* RTVs[] = { sceneColorRTV.get(), gbufferRTVs[0].get(), gbufferRTVs[1].get(), velocityMapRTV.get() };
		commandList->omSetRenderTargets(_countof(RTVs), RTVs, sceneDepthDSV.get());

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(sceneColorRTV.get(), clearColor);
		for (uint32 i = 0; i < NUM_GBUFFERS; ++i)
		{
			commandList->clearRenderTargetView(gbufferRTVs[i].get(), clearColor);
		}
		commandList->clearRenderTargetView(velocityMapRTV.get(), clearColor);
		commandList->clearDepthStencilView(sceneDepthDSV.get(), EDepthClearFlags::DEPTH_STENCIL, getDeviceFarDepth(), 0);

		BasePassInput passInput{
			.scene              = scene,
			.camera             = camera,
			.bIndirectDraw      = renderOptions.bEnableIndirectDraw,
			.bGPUCulling        = renderOptions.bEnableGPUCulling,
			.sceneUniformBuffer = sceneUniformCBV,
			.gpuScene           = gpuScene,
			.gpuCulling         = gpuCulling,
			.shadowMaskSRV      = shadowMaskSRV.get(),
		};
		basePass->renderBasePass(commandList, swapchainIndex, passInput);

		TextureMemoryBarrier barriersAfter[] = {
			{ ETextureMemoryLayout::RENDER_TARGET, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_gbuffers[0].get() },
			{ ETextureMemoryLayout::RENDER_TARGET, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_gbuffers[1].get() },
			{ ETextureMemoryLayout::RENDER_TARGET, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_velocityMap.get() },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersAfter), barriersAfter);
	}
	// Sky pass
	{
		SCOPED_DRAW_EVENT(commandList, SkyPass);

		RenderTargetView* RTVs[] = { sceneColorRTV.get() };
		commandList->omSetRenderTargets(_countof(RTVs), RTVs, sceneDepthDSV.get());

		SkyPassInput passInput{
			.sceneUniformBuffer = sceneUniformCBV,
			.skyboxSRV          = skyboxSRV.get(),
		};
		skyPass->renderSky(commandList, swapchainIndex, passInput);
	}

	// Path Tracing
	{
		SCOPED_DRAW_EVENT(commandList, PathTracing);

		TextureMemoryBarrier barriersBefore[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::UNORDERED_ACCESS, RT_pathTracing.get(), }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersBefore), barriersBefore);

		const bool keepDenoisingResult = renderOptions.pathTracingDenoiserState == EPathTracingDenoiserState::KeepDenoisingResult;

		if (bRenderPathTracing && !keepDenoisingResult)
		{
			PathTracingInput passInput{
				.scene                 = scene,
				.camera                = camera,
				.mode                  = renderOptions.pathTracing,
				.prevViewProjInvMatrix = prevSceneUniformData.viewProjInvMatrix,
				.prevViewProjMatrix    = prevSceneUniformData.viewProjMatrix,
				.bCameraHasMoved       = renderOptions.bCameraHasMoved,
				.sceneWidth            = sceneWidth,
				.sceneHeight           = sceneHeight,
				.gpuScene              = gpuScene,
				.bilateralBlur         = bilateralBlur,
				.raytracingScene       = accelStructure.get(),
				.sceneUniformBuffer    = sceneUniformCBV,
				.sceneColorTexture     = RT_pathTracing.get(),
				.sceneColorUAV         = pathTracingUAV.get(),
				.sceneDepthSRV         = sceneDepthSRV.get(),
				.prevSceneDepthSRV     = prevSceneDepthSRV.get(),
				.gbuffer0SRV           = gbufferSRVs[0].get(),
				.gbuffer1SRV           = gbufferSRVs[1].get(),
				.skyboxSRV             = skyboxSRV.get(),
			};
			pathTracingPass->renderPathTracing(commandList, swapchainIndex, passInput);
		}
	}
	// Path Tracing Denoising
	{
		const bool runDenoiserNow = renderOptions.pathTracingDenoiserState == EPathTracingDenoiserState::DenoiseNow;

		if (bRenderPathTracing && runDenoiserNow)
		{
			{
				SCOPED_DRAW_EVENT(commandList, BlitDenoiserInput);

				TextureMemoryBarrier barriers1[] = {
					{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_pathTracing.get(), }
				};
				commandList->resourceBarriers(0, nullptr, _countof(barriers1), barriers1);

				DenoiserPluginInput passInput{
					.imageWidth = sceneWidth,
					.imageHeight = sceneHeight,
					.sceneColorSRV = pathTracingSRV.get(),
					.gbuffer0SRV = gbufferSRVs[0].get(),
					.gbuffer1SRV = gbufferSRVs[1].get()
				};
				denoiserPluginPass->blitTextures(commandList, swapchainIndex, passInput);
			}
			{
				SCOPED_DRAW_EVENT(commandList, FlushCommandQueue);

				// #todo-oidn: Don't flush command queue
				// Flush GPU to readback input textures.
				immediateFlushCommandQueue(commandQueue, commandAllocator, commandList);
				resetCommandList(commandAllocator, commandList);
			}
			{
				SCOPED_DRAW_EVENT(commandList, ExecuteDenoiser);

				TextureMemoryBarrier barriers2[] = {
					{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::COPY_DEST, RT_pathTracing.get(), }
				};
				commandList->resourceBarriers(0, nullptr, _countof(barriers2), barriers2);

				denoiserPluginPass->executeDenoiser(commandList, RT_pathTracing.get());

				TextureMemoryBarrier barriers3[] = {
					{ ETextureMemoryLayout::COPY_DEST, ETextureMemoryLayout::UNORDERED_ACCESS, RT_pathTracing.get(), }
				};
				commandList->resourceBarriers(0, nullptr, _countof(barriers3), barriers3);
			}
		}
	}

	// Indirect Diffuse Reflection
	if (!bRenderIndirectDiffuse)
	{
		SCOPED_DRAW_EVENT(commandList, ClearIndirectDiffuse);

		TextureMemoryBarrier barriersBefore[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::RENDER_TARGET, RT_indirectDiffuse.get() }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersBefore), barriersBefore);

		// Clear as a render target, every frame. (not so ideal but works)
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(indirectDiffuseRTV.get(), clearColor);

		TextureMemoryBarrier barriersAfter[] = {
			{ ETextureMemoryLayout::RENDER_TARGET, ETextureMemoryLayout::UNORDERED_ACCESS, RT_indirectDiffuse.get() }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersAfter), barriersAfter);
	}
	else
	{
		SCOPED_DRAW_EVENT(commandList, IndirectDiffuse);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::UNORDERED_ACCESS, RT_indirectDiffuse.get() }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);
		
		IndirectDiffuseInput passInput{
			.scene                  = scene,
			.camera                 = camera,
			.mode                   = renderOptions.indirectDiffuse,
			.prevViewProjInvMatrix  = prevSceneUniformData.viewProjInvMatrix,
			.prevViewProjMatrix     = prevSceneUniformData.viewProjMatrix,
			.sceneWidth             = sceneWidth,
			.sceneHeight            = sceneHeight,
			.gpuScene               = gpuScene,
			.bilateralBlur          = bilateralBlur,
			.sceneUniformBuffer     = sceneUniformCBV,
			.raytracingScene        = accelStructure.get(),
			.skyboxSRV              = skyboxSRV.get(),
			.gbuffer0SRV            = gbufferSRVs[0].get(),
			.gbuffer1SRV            = gbufferSRVs[1].get(),
			.sceneDepthSRV          = sceneDepthSRV.get(),
			.prevSceneDepthSRV      = prevSceneDepthSRV.get(),
			.indirectDiffuseTexture = RT_indirectDiffuse.get(),
			.indirectDiffuseUAV     = indirectDiffuseUAV.get(),
		};
		indirectDiffusePass->renderIndirectDiffuse(commandList, swapchainIndex, passInput);
	}

	// Indirect Specular Reflection
	if (!bRenderIndirectSpecular)
	{
		SCOPED_DRAW_EVENT(commandList, ClearIndirectSpecular);

		TextureMemoryBarrier barriersBefore[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::RENDER_TARGET, RT_indirectSpecular.get() }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersBefore), barriersBefore);

		// Clear as a render target, every frame. (not so ideal but works)
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(indirectSpecularRTV.get(), clearColor);

		TextureMemoryBarrier barriersAfter[] = {
			{ ETextureMemoryLayout::RENDER_TARGET, ETextureMemoryLayout::UNORDERED_ACCESS, RT_indirectSpecular.get() }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersAfter), barriersAfter);
	}
	else
	{
		SCOPED_DRAW_EVENT(commandList, IndirectSpecular);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::UNORDERED_ACCESS, RT_indirectSpecular.get() }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);
		
		IndirectSpecularInput passInput{
			.scene                 = scene,
			.camera                = camera,
			.mode                  = renderOptions.indirectSpecular,
			.prevViewProjInvMatrix = prevSceneUniformData.viewProjInvMatrix,
			.prevViewProjMatrix    = prevSceneUniformData.viewProjMatrix,
			.bCameraHasMoved       = renderOptions.bCameraHasMoved,
			.sceneWidth            = sceneWidth,
			.sceneHeight           = sceneHeight,
			.sceneUniformBuffer    = sceneUniformCBV,
			.gpuScene              = gpuScene,
			.raytracingScene       = accelStructure.get(),
			.skyboxSRV             = skyboxSRV.get(),
			.gbuffer0SRV           = gbufferSRVs[0].get(),
			.gbuffer1SRV           = gbufferSRVs[1].get(),
			.sceneDepthSRV         = sceneDepthSRV.get(),
			.prevSceneDepthSRV     = prevSceneDepthSRV.get(),
			.indirectSpecularUAV   = indirectSpecularUAV.get(),
		};
		indirectSpecularPass->renderIndirectSpecular(commandList, swapchainIndex, passInput);
	}

	// Tone mapping
	// final target: back buffer
	{
		SCOPED_DRAW_EVENT(commandList, ToneMapping);

		TextureMemoryBarrier barriers[] = {
			{ ETextureMemoryLayout::RENDER_TARGET, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_sceneColor.get() },
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_indirectDiffuse.get() },
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_indirectSpecular.get() },
			{ ETextureMemoryLayout::UNORDERED_ACCESS, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_pathTracing.get() },
			{ ETextureMemoryLayout::PRESENT, ETextureMemoryLayout::RENDER_TARGET, swapchainBuffer, }
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriers), barriers);

		// #todo-renderer: Should not be here
		commandList->omSetRenderTarget(swapchainBufferRTV, nullptr);

		auto alternateSceneColorSRV = bRenderPathTracing ? pathTracingSRV.get() : sceneColorSRV.get();

		ToneMappingInput passInput{
			.viewport            = fullscreenViewport,
			.scissorRect         = fullscreenScissorRect,
			.sceneUniformCBV     = sceneUniformCBV,
			.sceneColorSRV       = alternateSceneColorSRV,
			.sceneDepthSRV       = sceneDepthSRV.get(),
			.gbuffer0SRV         = gbufferSRVs[0].get(),
			.gbuffer1SRV         = gbufferSRVs[1].get(),
			.indirectDiffuseSRV  = indirectDiffuseSRV.get(),
			.indirectSpecularSRV = indirectSpecularSRV.get(),
		};
		toneMapping->renderToneMapping(commandList, swapchainIndex, passInput);
	}

	// Buffer visualization
	// final target: back buffer
	if (renderOptions.bufferVisualization != EBufferVisualizationMode::None)
	{
		SCOPED_DRAW_EVENT(commandList, BufferVisualization);

		BufferVisualizationInput sources{
			.mode                = renderOptions.bufferVisualization,
			.gbuffer0SRV         = gbufferSRVs[0].get(),
			.gbuffer1SRV         = gbufferSRVs[1].get(),
			.sceneColorSRV       = sceneColorSRV.get(),
			.shadowMaskSRV       = shadowMaskSRV.get(),
			.indirectDiffuseSRV  = bRenderIndirectDiffuse ? indirectDiffuseSRV.get() : grey2DSRV.get(),
			.indirectSpecularSRV = bRenderIndirectSpecular ? indirectSpecularSRV.get() : grey2DSRV.get(),
			.velocityMapSRV      = velocityMapSRV.get(),
		};

		bufferVisualization->renderVisualization(commandList, swapchainIndex, sources);
	}

	// Store history
	{
		SCOPED_DRAW_EVENT(commandList, StoreFrameHistory);

		TextureMemoryBarrier barriersBefore[] = {
			{ ETextureMemoryLayout::DEPTH_STENCIL_TARGET, ETextureMemoryLayout::COPY_SRC, RT_sceneDepth.get() },
			{ ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, ETextureMemoryLayout::COPY_DEST, RT_prevSceneDepth.get() },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersBefore), barriersBefore);

		commandList->copyTexture2D(RT_sceneDepth.get(), RT_prevSceneDepth.get());

		TextureMemoryBarrier barriersAfter[] = {
			{ ETextureMemoryLayout::COPY_SRC, ETextureMemoryLayout::DEPTH_STENCIL_TARGET, RT_sceneDepth.get() },
			{ ETextureMemoryLayout::COPY_DEST, ETextureMemoryLayout::PIXEL_SHADER_RESOURCE, RT_prevSceneDepth.get() },
		};
		commandList->resourceBarriers(0, nullptr, _countof(barriersAfter), barriersAfter);
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

	TextureMemoryBarrier presentBarrier{ ETextureMemoryLayout::RENDER_TARGET, ETextureMemoryLayout::PRESENT, swapchainBuffer };
	commandList->resourceBarriers(0, nullptr, 1, &presentBarrier);

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

	// Deallocate memory, a bit messy
	commandList->executeDeferredDealloc();
	for (auto& cand : deferredCleanupList) delete cand.resource;
	deferredCleanupList.clear();
}

void SceneRenderer::recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight)
{
	gRenderDevice->getDenoiserDevice()->recreateResources(sceneWidth, sceneHeight);

	auto& cleanupList = this->deferredCleanupList;
	auto cleanup = [&cleanupList](GPUResource* resource) {
		if (resource != nullptr)
		{
			cleanupList.push_back({ resource });
		}
	};

	cleanup(RT_sceneColor.release());
	RT_sceneColor = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			PF_sceneColor,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV,
			sceneWidth, sceneHeight, 1, 1, 0)));
	RT_sceneColor->setDebugName(L"RT_SceneColor");

	sceneColorSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_sceneColor.get(),
		ShaderResourceViewDesc{
			.format              = RT_sceneColor->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_sceneColor->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	sceneColorRTV = UniquePtr<RenderTargetView>(device->createRTV(RT_sceneColor.get(),
		RenderTargetViewDesc{
			.format            = RT_sceneColor->getCreateParams().format,
			.viewDimension     = ERTVDimension::Texture2D,
			.texture2D         = Texture2DRTVDesc{
				.mipSlice      = 0,
				.planeSlice    = 0,
			},
		}
	));

	cleanup(RT_sceneDepth.release());
	sceneDepthDesc = TextureCreateParams::texture2D(
		DEPTH_TEXTURE_FORMAT, ETextureAccessFlags::DSV, sceneWidth, sceneHeight,
		1, 1, 0).setOptimalClearDepth(getDeviceFarDepth());
	RT_sceneDepth = UniquePtr<Texture>(device->createTexture(sceneDepthDesc));
	RT_sceneDepth->setDebugName(L"RT_SceneDepth");

	sceneDepthDSV = UniquePtr<DepthStencilView>(device->createDSV(RT_sceneDepth.get(),
		DepthStencilViewDesc{
			.format        = DEPTH_DSV_FORMAT,
			.viewDimension = EDSVDimension::Texture2D,
			.flags         = EDSVFlags::None,
			.texture2D     = Texture2DDSVDesc{ .mipSlice = 0 }
		}
	));
	sceneDepthSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_sceneDepth.get(),
		ShaderResourceViewDesc{
			.format              = DEPTH_SRV_FORMAT,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_sceneDepth->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));

	cleanup(RT_prevSceneDepth.release());
	TextureCreateParams prevSceneDepthDesc = sceneDepthDesc;
	prevSceneDepthDesc.accessFlags = ETextureAccessFlags::SRV;
	RT_prevSceneDepth = UniquePtr<Texture>(device->createTexture(prevSceneDepthDesc));
	RT_prevSceneDepth->setDebugName(L"RT_prevSceneDepth");
	prevSceneDepthSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_prevSceneDepth.get(),
		ShaderResourceViewDesc{
			.format              = DEPTH_SRV_FORMAT,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_prevSceneDepth->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));

	cleanup(RT_velocityMap.release());
	RT_velocityMap = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			PF_velocityMap,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV,
			sceneWidth, sceneHeight, 1, 1, 0)));
	RT_velocityMap->setDebugName(L"RT_VelocityMap");

	velocityMapSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_velocityMap.get(),
		ShaderResourceViewDesc{
			.format              = RT_velocityMap->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_velocityMap->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	velocityMapRTV = UniquePtr<RenderTargetView>(device->createRTV(RT_velocityMap.get(),
		RenderTargetViewDesc{
			.format            = RT_velocityMap->getCreateParams().format,
			.viewDimension     = ERTVDimension::Texture2D,
			.texture2D         = Texture2DRTVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	for (uint32 i = 0; i < NUM_GBUFFERS; ++i)
	{
		cleanup(RT_gbuffers[i].release());
		RT_gbuffers[i] = UniquePtr<Texture>(device->createTexture(
			TextureCreateParams::texture2D(
				PF_gbuffers[i],
				ETextureAccessFlags::RTV | ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
				sceneWidth, sceneHeight, 1, 1, 0)));
		std::wstring debugName = L"RT_GBuffer" + std::to_wstring(i);
		RT_gbuffers[i]->setDebugName(debugName.c_str());

		gbufferRTVs[i] = UniquePtr<RenderTargetView>(device->createRTV(RT_gbuffers[i].get(),
			RenderTargetViewDesc{
				.format            = PF_gbuffers[i],
				.viewDimension     = ERTVDimension::Texture2D,
				.texture2D         = Texture2DRTVDesc{
					.mipSlice      = 0,
					.planeSlice    = 0,
				},
			}
		));
		gbufferSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(RT_gbuffers[i].get(),
			ShaderResourceViewDesc{
				.format              = PF_gbuffers[i],
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = RT_gbuffers[i]->getCreateParams().mipLevels,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
		gbufferUAVs[i] = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(RT_gbuffers[i].get(),
			UnorderedAccessViewDesc{
				.format         = PF_gbuffers[i],
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{
					.mipSlice   = 0,
					.planeSlice = 0,
				},
			}
		));
	}

	cleanup(RT_shadowMask.release());
	RT_shadowMask = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::R32_FLOAT,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			sceneWidth, sceneHeight,
			1, 1, 0).setOptimalClearColor(1.0f, 1.0f, 1.0f, 1.0f)));
	RT_shadowMask->setDebugName(L"RT_ShadowMask");

	shadowMaskRTV = UniquePtr<RenderTargetView>(device->createRTV(RT_shadowMask.get(),
		RenderTargetViewDesc{
			.format            = RT_shadowMask->getCreateParams().format,
			.viewDimension     = ERTVDimension::Texture2D,
			.texture2D         = Texture2DRTVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));
	shadowMaskSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_shadowMask.get(),
		ShaderResourceViewDesc{
			.format              = RT_shadowMask->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_shadowMask->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	shadowMaskUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(RT_shadowMask.get(),
		UnorderedAccessViewDesc{
			.format         = RT_shadowMask->getCreateParams().format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	cleanup(RT_indirectDiffuse.release());
	RT_indirectDiffuse = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::R16G16B16A16_FLOAT,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			sceneWidth, sceneHeight, 1, 1, 0)));
	RT_indirectDiffuse->setDebugName(L"RT_IndirectDiffuse");

	indirectDiffuseSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_indirectDiffuse.get(),
		ShaderResourceViewDesc{
			.format              = RT_indirectDiffuse->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_indirectDiffuse->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	indirectDiffuseRTV = UniquePtr<RenderTargetView>(device->createRTV(RT_indirectDiffuse.get(),
		RenderTargetViewDesc{
			.format         = RT_indirectDiffuse->getCreateParams().format,
			.viewDimension  = ERTVDimension::Texture2D,
			.texture2D      = Texture2DRTVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));
	indirectDiffuseUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(RT_indirectDiffuse.get(),
		UnorderedAccessViewDesc{
			.format         = RT_indirectDiffuse->getCreateParams().format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	cleanup(RT_indirectSpecular.release());
	RT_indirectSpecular = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::R16G16B16A16_FLOAT,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			sceneWidth, sceneHeight, 1, 1, 0)));
	RT_indirectSpecular->setDebugName(L"RT_IndirectSpecular");

	indirectSpecularSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_indirectSpecular.get(),
		ShaderResourceViewDesc{
			.format              = RT_indirectSpecular->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_indirectSpecular->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	indirectSpecularRTV = UniquePtr<RenderTargetView>(device->createRTV(RT_indirectSpecular.get(),
		RenderTargetViewDesc{
			.format         = RT_indirectSpecular->getCreateParams().format,
			.viewDimension  = ERTVDimension::Texture2D,
			.texture2D      = Texture2DRTVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));
	indirectSpecularUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(RT_indirectSpecular.get(),
		UnorderedAccessViewDesc{
			.format         = RT_indirectSpecular->getCreateParams().format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	cleanup(RT_pathTracing.release());
	RT_pathTracing = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			EPixelFormat::R32G32B32A32_FLOAT,
			ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			sceneWidth, sceneHeight, 1, 1, 0)));
	RT_pathTracing->setDebugName(L"RT_PathTracing");

	pathTracingSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_pathTracing.get(),
		ShaderResourceViewDesc{
			.format              = RT_pathTracing->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_pathTracing->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	pathTracingUAV = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(RT_pathTracing.get(),
		UnorderedAccessViewDesc{
			.format         = RT_pathTracing->getCreateParams().format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	Texture* grey2D = gTextureManager->getSystemTextureGrey2D()->getGPUResource().get();
	grey2DSRV = UniquePtr<ShaderResourceView>(device->createSRV(grey2D,
		ShaderResourceViewDesc{
			.format              = grey2D->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = grey2D->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
}

void SceneRenderer::resetCommandList(RenderCommandAllocator* commandAllocator, RenderCommandList* commandList)
{
	commandAllocator->reset();
	commandList->reset(commandAllocator);
}

void SceneRenderer::immediateFlushCommandQueue(RenderCommandQueue* commandQueue, RenderCommandAllocator* commandAllocator, RenderCommandList* commandList)
{
	commandList->close();
	commandAllocator->markValid();
	commandQueue->executeCommandList(commandList);

	{
		SCOPED_CPU_EVENT(WaitForGPU);
		device->flushCommandQueue();
	}
}

void SceneRenderer::updateSceneUniform(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const SceneProxy* scene,
	const Camera* camera)
{
	const float sceneWidth = (float)gRenderDevice->getSwapChain()->getBackbufferWidth();
	const float sceneHeight = (float)gRenderDevice->getSwapChain()->getBackbufferHeight();

	sceneUniformData.viewMatrix            = camera->getViewMatrix();
	sceneUniformData.projMatrix            = camera->getProjMatrix();
	sceneUniformData.viewProjMatrix        = camera->getViewProjMatrix();

	sceneUniformData.viewInvMatrix         = camera->getViewInvMatrix();
	sceneUniformData.projInvMatrix         = camera->getProjInvMatrix();
	sceneUniformData.viewProjInvMatrix     = camera->getViewProjInvMatrix();

	sceneUniformData.prevViewProjMatrix    = prevSceneUniformData.viewProjMatrix;
	sceneUniformData.prevViewProjInvMatrix = prevSceneUniformData.viewProjInvMatrix;

	sceneUniformData.screenResolution[0]   = sceneWidth;
	sceneUniformData.screenResolution[1]   = sceneHeight;
	sceneUniformData.screenResolution[2]   = 1.0f / sceneWidth;
	sceneUniformData.screenResolution[3]   = 1.0f / sceneHeight;
	sceneUniformData.cameraFrustum         = camera->getFrustum();
	sceneUniformData.cameraPosition        = camera->getPosition();
	sceneUniformData.sunDirection          = scene->sun.direction;
	sceneUniformData.sunIlluminance        = scene->sun.illuminance;
	
	sceneUniformCBVs[swapchainIndex]->writeToGPU(commandList, &sceneUniformData, sizeof(sceneUniformData));

	memcpy_s(&prevSceneUniformData, sizeof(SceneUniform), &sceneUniformData, sizeof(SceneUniform));
}

void SceneRenderer::rebuildFrameResources(RenderCommandList* commandList, const SceneProxy* scene)
{
	// Create skybox SRV.
	Texture* skyboxWithFallback = scene->skyboxTexture.get();
	if (skyboxWithFallback == nullptr)
	{
		skyboxWithFallback = gTextureManager->getSystemTextureBlackCube()->getGPUResource().get();
	}
	if (skyboxSRV != nullptr)
	{
		commandList->enqueueDeferredDealloc(skyboxSRV.release());
	}
	skyboxSRV = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(skyboxWithFallback,
		ShaderResourceViewDesc{
			.format              = EPixelFormat::R8G8B8A8_UNORM,
			.viewDimension       = ESRVDimension::TextureCube,
			.textureCube         = TextureCubeSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = 1,
				.minLODClamp     = 0.0f
			}
		}
	));
}

void SceneRenderer::rebuildAccelerationStructure(RenderCommandList* commandList, const SceneProxy* scene)
{
	// - Entire scene is a TLAS that contains a list of BLAS instances.
	// - Each BLAS contains all sections of each StaticMesh.

	const uint32 numStaticMeshes = (uint32)scene->staticMeshes.size();

	// Prepare BLAS instances.
	std::vector<BLASInstanceInitDesc> blasDescArray(numStaticMeshes);
	for (uint32 staticMeshIndex = 0; staticMeshIndex < numStaticMeshes; ++staticMeshIndex)
	{
		StaticMeshProxy* staticMesh = scene->staticMeshes[staticMeshIndex];
		BLASInstanceInitDesc& blasDesc = blasDescArray[staticMeshIndex];

		Float4x4 modelMatrix = staticMesh->getLocalToWorld(); // row-major
		memcpy(blasDesc.instanceTransform[0], modelMatrix.m[0], sizeof(float) * 4);
		memcpy(blasDesc.instanceTransform[1], modelMatrix.m[1], sizeof(float) * 4);
		memcpy(blasDesc.instanceTransform[2], modelMatrix.m[2], sizeof(float) * 4);

		for (const StaticMeshSection& section : staticMesh->getSections())
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

	if (accelStructure != nullptr) commandList->enqueueDeferredDealloc(accelStructure.release());
	// Build acceleration structure.
	accelStructure = UniquePtr<AccelerationStructure>(
		commandList->buildRaytracingAccelerationStructure((uint32)blasDescArray.size(), blasDescArray.data()));
}
