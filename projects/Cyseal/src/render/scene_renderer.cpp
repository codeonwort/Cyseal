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

#include "render/renderer_constants.h"
#include "render/static_mesh.h"
#include "render/util/clear_resource_pass.h"
#include "render/gpu_scene.h"
#include "render/gpu_culling.h"
#include "render/bilateral_blur.h"
#include "render/depth_prepass.h"
#include "render/decode_vis_buffer_pass.h"
#include "render/base_pass.h"
#include "render/hiz_pass.h"
#include "render/sky_pass.h"
#include "render/combine_lighting_pass.h"
#include "render/tone_mapping.h"
#include "render/buffer_visualization.h"
#include "render/store_history_pass.h"
#include "render/raytracing/ray_traced_shadows.h"
#include "render/raytracing/indirect_diffuse_pass.h"
#include "render/raytracing/indirect_specular_pass.h"
#include "render/pathtracing/path_tracing_pass.h"
#include "render/pathtracing/denoiser_plugin_pass.h"
#include "render/optical_flow_pass.h"
#include "render/frame_gen_pass.h"
#include "render/final_blit_pass.h"

#include "util/profiling.h"

#include <thread>

#define SCENE_UNIFORM_MEMORY_POOL_SIZE (64 * 1024) // 64 KiB
#define MAX_CULL_OPERATIONS            (2 * kMaxBasePassPermutation) // depth prepass + base pass
#define MAX_FINAL_BLIT_OPERATIONS      2 // Actual count: 2 if frame generation is enabled, 1 otherwise.
#define AVG_RENDER_TIME_WINDOW_SIZE    16

static uint32 fullMipCount(uint32 width, uint32 height)
{
	return static_cast<uint32>(floor(log2(std::max(width, height))) + 1);
}

void SceneRenderer::initialize(RenderDevice* renderDevice)
{
	device = renderDevice;
	frameID = 0;

	avgRenderTime.init(AVG_RENDER_TIME_WINDOW_SIZE);

	// Scene textures: Don't create yet. You invoke recreateSceneTextures() before using scene renderer.
	// recreateSceneTextures(width, height);

	// Scene uniforms
	{
		const uint32 swapchainCount = renderDevice->maxFramesInFlight();
		CHECK(sizeof(SceneUniform) * swapchainCount <= SCENE_UNIFORM_MEMORY_POOL_SIZE);

		sceneUniformMemory = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = SCENE_UNIFORM_MEMORY_POOL_SIZE,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::CPU_WRITE | EBufferAccessFlags::CBV,
			}
		));

		sceneUniformDescriptorHeap = UniquePtr<DescriptorHeap>(device->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV,
				.numDescriptors = swapchainCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
				.purpose        = EDescriptorHeapPurpose::Volatile,
			}
		));

		uint32 bufferOffset = 0;
		sceneUniformCBVs.initialize(swapchainCount);
		for (uint32 i = 0; i < swapchainCount; ++i)
		{
			sceneUniformCBVs[i] = UniquePtr<ConstantBufferView>(
				device->createCBV(
					sceneUniformMemory.get(),
					sceneUniformDescriptorHeap.get(),
					sizeof(SceneUniform),
					bufferOffset));

			uint32 alignment = device->getConstantBufferDataAlignment();
			bufferOffset += Cymath::alignBytes(sizeof(SceneUniform), alignment);
		}
	}

	// Render passes
	{
		sceneRenderPasses.push_back(clearResourcePass = new(EMemoryTag::Renderer) ClearResourcePass);
		sceneRenderPasses.push_back(gpuScene = new(EMemoryTag::Renderer) GPUScene);
		sceneRenderPasses.push_back(gpuCulling = new(EMemoryTag::Renderer) GPUCulling);
		sceneRenderPasses.push_back(bilateralBlur = new(EMemoryTag::Renderer) BilateralBlur);
		sceneRenderPasses.push_back(rayTracedShadowsPass = new(EMemoryTag::Renderer) RayTracedShadowsPass);
		sceneRenderPasses.push_back(depthPrepass = new(EMemoryTag::Renderer) DepthPrepass);
		sceneRenderPasses.push_back(decodeVisBufferPass = new(EMemoryTag::Renderer) DecodeVisBufferPass);
		sceneRenderPasses.push_back(basePass = new(EMemoryTag::Renderer) BasePass);
		sceneRenderPasses.push_back(hizPass = new(EMemoryTag::Renderer) HiZPass);
		sceneRenderPasses.push_back(skyPass = new(EMemoryTag::Renderer) SkyPass);
		sceneRenderPasses.push_back(indirectDiffusePass = new(EMemoryTag::Renderer) IndirectDiffusePass);
		sceneRenderPasses.push_back(indirectSpecularPass = new(EMemoryTag::Renderer) IndirecSpecularPass);
		sceneRenderPasses.push_back(combineLightingPass = new(EMemoryTag::Renderer) CombineLightingPass);
		sceneRenderPasses.push_back(toneMapping = new(EMemoryTag::Renderer) ToneMapping);
		sceneRenderPasses.push_back(bufferVisualization = new(EMemoryTag::Renderer) BufferVisualization);
		sceneRenderPasses.push_back(pathTracingPass = new(EMemoryTag::Renderer) PathTracingPass);
		sceneRenderPasses.push_back(denoiserPluginPass = new(EMemoryTag::Renderer) DenoiserPluginPass);
		sceneRenderPasses.push_back(storeHistoryPass = new(EMemoryTag::Renderer) StoreHistoryPass);
		sceneRenderPasses.push_back(opticalFlowPass = new(EMemoryTag::Renderer) OpticalFlowPass);
		sceneRenderPasses.push_back(frameGenPass = new(EMemoryTag::Renderer) FrameGenPass);
		sceneRenderPasses.push_back(finalBlitPass = new(EMemoryTag::Renderer) FinalBlitPass);

		clearResourcePass->initialize(renderDevice);
		gpuScene->initialize(renderDevice);
		gpuCulling->initialize(renderDevice, MAX_CULL_OPERATIONS);
		bilateralBlur->initialize(renderDevice);
		rayTracedShadowsPass->initialize(renderDevice);
		depthPrepass->initialize(renderDevice, PF_visibilityBuffer);
		decodeVisBufferPass->initialize(renderDevice);
		basePass->initialize(renderDevice, PF_sceneColor, PF_gbuffers, NUM_GBUFFERS, PF_velocityMap);
		hizPass->initialize(renderDevice);
		skyPass->initialize(renderDevice, PF_sceneColor);
		indirectDiffusePass->initialize(renderDevice);
		indirectSpecularPass->initialize(renderDevice);
		combineLightingPass->initialize(renderDevice, PF_sceneColor);
		toneMapping->initialize(renderDevice);
		bufferVisualization->initialize(renderDevice);
		pathTracingPass->initialize(renderDevice);
		denoiserPluginPass->initialize(renderDevice);
		storeHistoryPass->initialize(renderDevice);
		opticalFlowPass->initialize(renderDevice);
		frameGenPass->initialize(renderDevice, PF_finalSceneColor);
		finalBlitPass->initialize(renderDevice, MAX_FINAL_BLIT_OPERATIONS);
	}
}

void SceneRenderer::destroy()
{
	RT_visibilityBuffer.reset();
	RT_barycentricCoord.reset();
	for (uint32 i = 0; i < NUM_GBUFFERS; ++i) RT_visGbuffers[i].reset();
	RT_sceneColor.reset();
	RT_sceneDepth.reset();
	RT_prevSceneDepth.reset();
	RT_hiz.reset();
	RT_velocityMap.reset();
	for (uint32 i = 0; i < NUM_GBUFFERS; ++i) RT_gbuffers[i].reset();
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
	const bool bRenderToBackbuffer = renderOptions.renderToBackbuffer();

	SwapChain*        swapChain          = nullptr;
	SwapChainImage*   swapchainBuffer    = nullptr;
	RenderTargetView* swapchainBufferRTV = nullptr;

	auto acquireSwapchainResources = [&](SwapChainImage*& outBuffer, RenderTargetView*& outRTV) {
		uint32 swapchainIndex = swapChain->getCurrentBackbufferIndex();
		outBuffer = swapChain->getSwapchainBuffer(swapchainIndex);
		outRTV = swapChain->getSwapchainBufferRTV(swapchainIndex);
	};

	if (bRenderToBackbuffer)
	{
		swapChain = device->getSwapChain();
		CHECK(swapChain != nullptr);

		swapChain->prepareBackbuffer();

		acquireSwapchainResources(swapchainBuffer, swapchainBufferRTV);
	}

	// #wip: Replace all frameIndex accesses with frameInfo.
	const uint32 frameIndex = (frameID % device->maxFramesInFlight());

	auto commandAllocator     = device->getCommandAllocator(frameIndex);
	auto commandList          = device->getCommandList(frameIndex);
	auto commandQueue         = device->getCommandQueue();

	createFinalBlitRTV(commandList, renderOptions);

	const uint32      unscaledRenderWidth  = renderResolutionX;
	const uint32      unscaledRenderHeight = renderResolutionY;
	const uint32      resolutionScale      = renderOptions.getResolutionScale();
	const uint32      sceneWidth           = (resolutionScale == 100) ? unscaledRenderWidth : (uint32)(0.01f * (float)(resolutionScale * unscaledRenderWidth));
	const uint32      sceneHeight          = (resolutionScale == 100) ? unscaledRenderHeight : (uint32)(0.01f * (float)(resolutionScale * unscaledRenderHeight));

	TextureKind*      finalBlitTarget = renderOptions.finalRenderTarget;
	RenderTargetView* finalBlitRTV    = finalRenderTargetRTV.get();
	uint32            finalBlitWidth  = 0;
	uint32            finalBlitHeight = 0;
	if (bRenderToBackbuffer)
	{
		finalBlitTarget    = swapchainBuffer;
		finalBlitRTV       = swapchainBufferRTV;
		finalBlitWidth     = swapChain->getBackbufferWidth();
		finalBlitHeight    = swapChain->getBackbufferHeight();
	}
	else
	{
		finalBlitWidth     = renderOptions.finalRenderTarget->getCreateParams().width;
		finalBlitHeight    = renderOptions.finalRenderTarget->getCreateParams().height;
	}

	const bool bRenderDepthPrepass = renderOptions.bEnableDepthPrepass;
	const bool bRenderVisibilityBuffer = renderOptions.bEnableVisibilityBuffer;

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

	const bool bRenderFrameGeneration = renderOptions.bGenerateFrame && !bRenderPathTracing && bRenderToBackbuffer;

	const FrameInfo frameInfo{
		.frameID    = frameID,
		.frameIndex = (frameID % device->maxFramesInFlight()),
	};

	clearResourcePass->prepareForFrame(frameInfo);

	rebuildFrameResources(commandList, scene);

	resetCommandList(commandAllocator, commandList);

	// Just execute prior to any standard renderer works.
	// If some custom commands should execute in midst of frame rendering,
	// I need to insert delegates here and there of this SceneRenderer::render() function.
	for (RenderCommandList::CustomCommandType lambda : customCommands)
	{
		lambda(*commandList);
	}
	customCommands.clear();

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

	updateSceneUniform(commandList, frameIndex, scene, camera, sceneWidth, sceneHeight);
	auto sceneUniformCBV = sceneUniformCBVs.at(frameIndex);

	{
		SCOPED_DRAW_EVENT(commandList, GPUScene);

		GPUSceneInput passInput{
			.scene  = scene,
			.camera = camera,
		};
		gpuScene->renderGPUScene(commandList, frameIndex, passInput);
		gpuScene->generateDrawcalls(commandList, frameIndex, passInput);
	}

	if (renderOptions.bEnableGPUCulling)
	{
		gpuCulling->resetCullingResources();
	}

	if (bSupportsRaytracing && scene->bRebuildRaytracingScene)
	{
		SCOPED_DRAW_EVENT(commandList, CreateRaytracingScene);

		// Recreate every BLAS
		rebuildAccelerationStructure(commandList, scene);

		GlobalBarrier globalBarrier = {
			EBarrierSync::BUILD_RAYTRACING_ACCELERATION_STRUCTURE, EBarrierSync::RAYTRACING | EBarrierSync::COMPUTE_SHADING,
			EBarrierAccess::RAYTRACING_ACCELERATION_STRUCTURE_WRITE, EBarrierAccess::RAYTRACING_ACCELERATION_STRUCTURE_READ
		};
		commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
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

		GlobalBarrier globalBarrier = {
			EBarrierSync::BUILD_RAYTRACING_ACCELERATION_STRUCTURE, EBarrierSync::RAYTRACING | EBarrierSync::COMPUTE_SHADING,
			EBarrierAccess::RAYTRACING_ACCELERATION_STRUCTURE_WRITE, EBarrierAccess::RAYTRACING_ACCELERATION_STRUCTURE_READ
		};
		commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
	}

	if (bRenderDepthPrepass)
	{
		SCOPED_DRAW_EVENT(commandList, DepthPrepass);

		TextureBarrierAuto barriers[] = {
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				RT_visibilityBuffer.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
			{
				EBarrierSync::DEPTH_STENCIL, EBarrierAccess::DEPTH_STENCIL_WRITE, EBarrierLayout::DepthStencilWrite,
				RT_sceneDepth.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(barriers), barriers, 0, nullptr);

		if (bRenderVisibilityBuffer)
		{
			float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f }; // RT format is R32_UINT but we can pass only floats. It's zero so no problem here.
			RenderTargetView* RTVs[] = { visibilityBufferRTV.get() };
			commandList->omSetRenderTargets(_countof(RTVs), RTVs, sceneDepthDSV.get());
			commandList->clearRenderTargetView(RTVs[0], clearColor);
			commandList->clearDepthStencilView(sceneDepthDSV.get(), EDepthClearFlags::DEPTH_STENCIL, getDeviceFarDepth(), 0);
		}
		else
		{
			commandList->omSetRenderTargets(0, nullptr, sceneDepthDSV.get());
			commandList->clearDepthStencilView(sceneDepthDSV.get(), EDepthClearFlags::DEPTH_STENCIL, getDeviceFarDepth(), 0);
		}

		DepthPrepassInput passInput{
			.scene              = scene,
			.camera             = camera,
			.indirectDrawMode   = renderOptions.indirectDrawMode,
			.bGPUCulling        = renderOptions.bEnableGPUCulling,
			.bVisibilityBuffer  = bRenderVisibilityBuffer,
			.sceneUniformBuffer = sceneUniformCBV,
			.gpuScene           = gpuScene,
			.gpuCulling         = gpuCulling,
		};
		depthPrepass->renderDepthPrepass(commandList, frameIndex, passInput);
	}

	if (bRenderVisibilityBuffer)
	{
		SCOPED_DRAW_EVENT(commandList, DecodeVisibilityBuffer);

		DecodeVisBufferPassInput passInput{
			.textureWidth       = sceneWidth,
			.textureHeight      = sceneHeight,
			.gpuScene           = gpuScene,
			.sceneUniformBuffer = sceneUniformCBV,
			.sceneDepthTexture  = RT_sceneDepth.get(),
			.sceneDepthSRV      = sceneDepthSRV.get(),
			.visBufferTexture   = RT_visibilityBuffer.get(),
			.visBufferSRV       = visibilityBufferSRV.get(),
			.barycentricTexture = RT_barycentricCoord.get(),
			.barycentricUAV     = barycentricCoordUAV.get(),
			.visGBuffer0        = RT_visGbuffers[0].get(),
			.visGBuffer1        = RT_visGbuffers[1].get(),
			.visGBuffer0UAV     = visGbufferUAVs[0].get(),
			.visGBuffer1UAV     = visGbufferUAVs[1].get(),
		};

		decodeVisBufferPass->decodeVisBuffer(commandList, frameIndex, passInput);
	}

	// Ray Traced Shadows
	if (!bRenderRayTracedShadows)
	{
		SCOPED_DRAW_EVENT(commandList, ClearRayTracedShadows);

		TextureBarrierAuto barriersBefore[] = {
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				RT_shadowMask.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			}
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		// Clear as a render target. (not so ideal but works)
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		commandList->clearRenderTargetView(shadowMaskRTV.get(), clearColor);
	}
	else
	{
		SCOPED_DRAW_EVENT(commandList, RayTracedShadows);

		TextureBarrierAuto barriersBefore[] = {
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				RT_shadowMask.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			}
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

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
		rayTracedShadowsPass->renderRayTracedShadows(commandList, frameIndex, passInput);
	}

	// Base pass
	{
		SCOPED_DRAW_EVENT(commandList, BasePass);

		TextureBarrierAuto barriers[] = {
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				RT_sceneColor.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				RT_gbuffers[0].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				RT_gbuffers[1].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				RT_velocityMap.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
			{
				EBarrierSync::DEPTH_STENCIL, EBarrierAccess::DEPTH_STENCIL_WRITE, EBarrierLayout::DepthStencilWrite,
				RT_sceneDepth.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(barriers), barriers, 0, nullptr);

		RenderTargetView* RTVs[] = { sceneColorRTV.get(), gbufferRTVs[0].get(), gbufferRTVs[1].get(), velocityMapRTV.get() };
		commandList->omSetRenderTargets(_countof(RTVs), RTVs, sceneDepthDSV.get());

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(sceneColorRTV.get(), clearColor);
		for (uint32 i = 0; i < NUM_GBUFFERS; ++i)
		{
			commandList->clearRenderTargetView(gbufferRTVs[i].get(), clearColor);
		}
		commandList->clearRenderTargetView(velocityMapRTV.get(), clearColor);

		if (!bRenderDepthPrepass)
		{
			commandList->clearDepthStencilView(sceneDepthDSV.get(), EDepthClearFlags::DEPTH_STENCIL, getDeviceFarDepth(), 0);
		}

		BasePassInput passInput{
			.scene              = scene,
			.camera             = camera,
			.indirectDrawMode   = renderOptions.indirectDrawMode,
			.bGPUCulling        = renderOptions.bEnableGPUCulling,
			.sceneUniformBuffer = sceneUniformCBV,
			.gpuScene           = gpuScene,
			.gpuCulling         = gpuCulling,
			.shadowMaskSRV      = shadowMaskSRV.get(),
		};
		basePass->renderBasePass(commandList, frameIndex, passInput);
	}

	Texture* currentGBufferTexture0 = nullptr;
	Texture* currentGBufferTexture1 = nullptr;
	ShaderResourceView* currentGBufferSRV0 = nullptr;
	ShaderResourceView* currentGBufferSRV1 = nullptr;
	if (bRenderVisibilityBuffer)
	{
		currentGBufferTexture0 = RT_visGbuffers[0].get();
		currentGBufferTexture1 = RT_visGbuffers[1].get();
		currentGBufferSRV0 = visGbufferSRVs[0].get();
		currentGBufferSRV1 = visGbufferSRVs[1].get();
	}
	else
	{
		currentGBufferTexture0 = RT_gbuffers[0].get();
		currentGBufferTexture1 = RT_gbuffers[1].get();
		currentGBufferSRV0 = gbufferSRVs[0].get();
		currentGBufferSRV1 = gbufferSRVs[1].get();
	}

	// Store history pass (step 1. There's step 2 below)
	{
		SCOPED_DRAW_EVENT(commandList, StoreHistoryPass_Current);

		StoreHistoryPassInput passInput{
			.textureWidth         = sceneWidth,
			.textureHeight        = sceneHeight,
			.gbuffer0             = currentGBufferTexture0,
			.gbuffer1             = currentGBufferTexture1,
			.gbuffer0SRV          = currentGBufferSRV0,
			.gbuffer1SRV          = currentGBufferSRV1,
		};
		storeHistoryPass->extractCurrent(commandList, frameIndex, passInput);
	}

	// HiZ pass
	{
		SCOPED_DRAW_EVENT(commandList, HiZPass);

		HiZPassInput passInput{
			.textureWidth      = sceneWidth,
			.textureHeight     = sceneHeight,
			.sceneDepthTexture = RT_sceneDepth.get(),
			.sceneDepthSRV     = sceneDepthSRV.get(),
			.hizTexture        = RT_hiz.get(),
			.hizSRV            = hizSRV.get(),
			.hizUAVs           = hizUAVs,
		};
		hizPass->renderHiZ(commandList, frameIndex, passInput);
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
		skyPass->renderSky(commandList, frameIndex, passInput);
	}

	// Path Tracing
	{
		SCOPED_DRAW_EVENT(commandList, PathTracing);

		TextureBarrierAuto barriersBefore[] = {
			{
				EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
				RT_pathTracing.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		const bool keepDenoisingResult = renderOptions.pathTracingDenoiserState == EPathTracingDenoiserState::KeepDenoisingResult;

		if (bRenderPathTracing && !keepDenoisingResult)
		{
			PathTracingInput passInput{
				.scene                 = scene,
				.camera                = camera,
				.mode                  = renderOptions.pathTracing,
				.kernel                = renderOptions.pathTracingKernel,
				.randomSeed            = renderOptions.pathTracingRandomSeed,
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
				.velocityMapSRV        = velocityMapSRV.get(),
				.gbuffer0SRV           = currentGBufferSRV0,
				.gbuffer1SRV           = currentGBufferSRV1,
				.skyboxSRV             = skyboxSRV.get(),
			};
			pathTracingPass->renderPathTracing(commandList, frameIndex, passInput);
		}
	}
	// Path Tracing Denoising
	{
		const bool runDenoiserNow = renderOptions.pathTracingDenoiserState == EPathTracingDenoiserState::DenoiseNow;

		if (bRenderPathTracing && runDenoiserNow)
		{
			{
				SCOPED_DRAW_EVENT(commandList, BlitDenoiserInput);

				TextureBarrierAuto barriers1[] = {
					{
						EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
						RT_pathTracing.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
					}
				};
				commandList->barrierAuto(0, nullptr, _countof(barriers1), barriers1, 0, nullptr);

				DenoiserPluginInput passInput{
					.imageWidth    = sceneWidth,
					.imageHeight   = sceneHeight,
					.sceneColorSRV = pathTracingSRV.get(),
					.gbuffer0SRV   = currentGBufferSRV0,
					.gbuffer1SRV   = currentGBufferSRV1,
				};
				denoiserPluginPass->blitTextures(commandList, frameIndex, passInput);
			}
			{
				SCOPED_DRAW_EVENT(commandList, FlushCommandQueue);

				// Flush GPU to readback input textures.
				immediateFlushCommandQueue(commandQueue, commandAllocator, commandList);
				resetCommandList(commandAllocator, commandList);
			}
			{
				SCOPED_DRAW_EVENT(commandList, ExecuteDenoiser);

				TextureBarrierAuto barriers2[] = {
					{
						EBarrierSync::COPY, EBarrierAccess::COPY_DEST, EBarrierLayout::CopyDest,
						RT_pathTracing.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
					}
				};
				commandList->barrierAuto(0, nullptr, _countof(barriers2), barriers2, 0, nullptr);

				denoiserPluginPass->executeDenoiser(commandList, sceneWidth, sceneHeight, RT_pathTracing.get());
			}
		}
	}

	// Indirect Diffuse Reflection
	if (!bRenderIndirectDiffuse)
	{
		SCOPED_DRAW_EVENT(commandList, ClearIndirectDiffuse);

		TextureBarrierAuto barriersBefore[] = {
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				RT_indirectDiffuse.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		// Clear as a render target, every frame. (not so ideal but works)
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(indirectDiffuseRTV.get(), clearColor);
	}
	else
	{
		SCOPED_DRAW_EVENT(commandList, IndirectDiffuse);

		IndirectDiffuseInput passInput{
			.scene                  = scene,
			.mode                   = renderOptions.indirectDiffuse,
			.randomSeed             = renderOptions.indirectDiffuseRandomSeed,
			.unscaledRenderWidth    = unscaledRenderWidth,
			.unscaledRenderHeight   = unscaledRenderHeight,
			.sceneWidth             = sceneWidth,
			.sceneHeight            = sceneHeight,
			.gpuScene               = gpuScene,
			.bilateralBlur          = bilateralBlur,
			.sceneUniformBuffer     = sceneUniformCBV,
			.raytracingScene        = accelStructure.get(),
			.skyboxSRV              = skyboxSRV.get(),
			.gbuffer0SRV            = currentGBufferSRV0,
			.gbuffer1SRV            = currentGBufferSRV1,
			.sceneDepthSRV          = sceneDepthSRV.get(),
			.prevSceneDepthSRV      = prevSceneDepthSRV.get(),
			.velocityMapSRV         = velocityMapSRV.get(),
			.indirectDiffuseTexture = RT_indirectDiffuse.get(),
			.indirectDiffuseUAV     = indirectDiffuseUAV.get(),
		};
		indirectDiffusePass->renderIndirectDiffuse(commandList, frameIndex, passInput);
	}

	// Indirect Specular Reflection
	if (!bRenderIndirectSpecular)
	{
		SCOPED_DRAW_EVENT(commandList, ClearIndirectSpecular);

		TextureBarrierAuto barriersBefore[] = {
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				RT_indirectSpecular.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			}
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		// Clear as a render target, every frame. (not so ideal but works)
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->clearRenderTargetView(indirectSpecularRTV.get(), clearColor);
	}
	else
	{
		SCOPED_DRAW_EVENT(commandList, IndirectSpecular);

		StoreHistoryPassResources historyResources = storeHistoryPass->getResources(frameIndex);
		
		IndirectSpecularInput passInput{
			.scene                   = scene,
			.mode                    = renderOptions.indirectSpecular,
			.randomSeed              = renderOptions.indirectSpecularRandomSeed,
			.unscaledRenderWidth     = unscaledRenderWidth,
			.unscaledRenderHeight    = unscaledRenderHeight,
			.sceneWidth              = sceneWidth,
			.sceneHeight             = sceneHeight,
			.invProjection           = sceneUniformData.projInvMatrix,
			.invView                 = sceneUniformData.viewInvMatrix,
			.prevViewProjection      = sceneUniformData.prevViewProjMatrix,
			.sceneUniformBuffer      = sceneUniformCBV,
			.gpuScene                = gpuScene,
			.raytracingScene         = accelStructure.get(),
			.skyboxSRV               = skyboxSRV.get(),
			.gbuffer0Texture         = currentGBufferTexture0,
			.gbuffer1Texture         = currentGBufferTexture1,
			.gbuffer0SRV             = currentGBufferSRV0,
			.gbuffer1SRV             = currentGBufferSRV1,
			.normalTexture           = historyResources.currNormal,
			.normalSRV               = historyResources.currNormalSRV,
			.roughnessTexture        = historyResources.currRoughness,
			.roughnessSRV            = historyResources.currRoughnessSRV,
			.prevNormalTexture       = historyResources.prevNormal,
			.prevNormalSRV           = historyResources.prevNormalSRV,
			.prevRoughnessTexture    = historyResources.prevRoughness,
			.prevRoughnessSRV        = historyResources.prevRoughnessSRV,
			.sceneDepthTexture       = RT_sceneDepth.get(),
			.sceneDepthSRV           = sceneDepthSRV.get(),
			.prevSceneDepthTexture   = RT_prevSceneDepth.get(),
			.prevSceneDepthSRV       = prevSceneDepthSRV.get(),
			.hizTexture              = RT_hiz.get(),
			.hizSRV                  = hizSRV.get(),
			.velocityMapTexture      = RT_velocityMap.get(),
			.velocityMapSRV          = velocityMapSRV.get(),
			.tileCoordBuffer         = indirectSpecularTileCoordBuffer.get(),
			.tileCounterBuffer       = indirectSpecularTileCounterBuffer.get(),
			.tileCoordBufferUAV      = indirectSpecularTileCoordBufferUAV.get(),
			.tileCounterBufferUAV    = indirectSpecularTileCounterBufferUAV.get(),
			.indirectSpecularTexture = RT_indirectSpecular.get(),
		};
		indirectSpecularPass->renderIndirectSpecular(commandList, frameIndex, passInput);
	}

	if (!bRenderPathTracing)
	{
		SCOPED_DRAW_EVENT(commandList, CombineLighting);

		CombineLightingPassInput passInput{
			.sceneUniformCBV         = sceneUniformCBV,
			.sceneColorTexture       = RT_sceneColor.get(),
			.sceneColorRTV           = sceneColorRTV.get(),
			.sceneDepthTexture       = RT_sceneDepth.get(),
			.sceneDepthSRV           = sceneDepthSRV.get(),
			.gbuffer0Texture         = RT_gbuffers[0].get(),
			.gbuffer0SRV             = currentGBufferSRV0,
			.gbuffer1Texture         = RT_gbuffers[1].get(),
			.gbuffer1SRV             = currentGBufferSRV1,
			.indirectDiffuseTexture  = RT_indirectDiffuse.get(),
			.indirectDiffuseSRV      = indirectDiffuseSRV.get(),
			.indirectSpecularTexture = RT_indirectSpecular.get(),
			.indirectSpecularSRV     = indirectSpecularSRV.get(),
		};
		combineLightingPass->combineLighting(commandList, frameIndex, passInput);
	}

	// Store history pass (step 2)
	{
		SCOPED_DRAW_EVENT(commandList, StoreHistoryPass_Prev);

		TextureBarrierAuto textureBarriers[] = {
			{
				EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, EBarrierLayout::CopySource,
				RT_sceneDepth.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
			{
				EBarrierSync::COPY, EBarrierAccess::COPY_DEST, EBarrierLayout::CopyDest,
				RT_prevSceneDepth.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

		commandList->copyTexture2D(RT_sceneDepth.get(), RT_prevSceneDepth.get());

		storeHistoryPass->copyCurrentToPrev(commandList, frameIndex);
	}

	// Set final color as render target.
	{
		SCOPED_DRAW_EVENT(commandList, SetFinalColorAsRenderTarget);

		TextureBarrierAuto barrier{
			EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
			RT_finalSceneColor.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		};
		commandList->barrierAuto(0, nullptr, 1, &barrier, 0, nullptr);

		commandList->omSetRenderTarget(finalSceneColorRTV.get(), nullptr);
	}

	// Tone mapping
	{
		SCOPED_DRAW_EVENT(commandList, ToneMapping);

		TextureBarrierAuto barriers[] = {
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_sceneColor.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_pathTracing.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(barriers), barriers, 0, nullptr);

		auto alternateSceneColorSRV = bRenderPathTracing ? pathTracingSRV.get() : sceneColorSRV.get();

		ToneMappingInput passInput{
			.renderTarget        = RT_finalSceneColor.get(),
			.viewport            = fullscreenViewport,
			.scissorRect         = fullscreenScissorRect,
			.sceneUniformCBV     = sceneUniformCBV,
			.sceneColorSRV       = alternateSceneColorSRV,
		};
		toneMapping->renderToneMapping(commandList, frameIndex, passInput);
	}

	OpticalFlowPassOutput opticalFlowPassOutput{};
	FrameGenPassOutput frameGenPassOutput{};
	if (bRenderFrameGeneration)
	{
		const bool bResetOpticalFlowAccumulation = false;

		// #todo-fsr3: Change transfer func and min/max luminance when HDR display support is implemented.
		// Currently backbuffer is always in LDR so just hard-code them.
		const auto backbufferTransferFunction = OpticalFlowBackbufferTransferFunction::LinearLdrToLuminance;
		const float fMinLuminance = 0.0f;
		const float fMaxLuminance = 1.0f;

		auto frameGenInputColorTexture = RT_finalSceneColor.get();
		auto frameGenInputColorSRV = finalSceneColorSRV.get();

		{
			SCOPED_DRAW_EVENT(commandList, OpticalFlow);

			OpticalFlowPassInput passInput{
				.clearResourcePass  = clearResourcePass,
				.transferFunction   = backbufferTransferFunction,
				.bResetAccumulation = bResetOpticalFlowAccumulation,
				.containerSizeX     = unscaledRenderWidth,
				.containerSizeY     = unscaledRenderHeight,
				.lumaResolutionX    = (int32)sceneWidth,
				.lumaResolutionY    = (int32)sceneHeight,
				.minLuminance       = fMinLuminance,
				.maxLuminance       = fMaxLuminance,
				.sceneColorTexture  = frameGenInputColorTexture,
				.sceneColorSRV      = frameGenInputColorSRV,
			};
			opticalFlowPassOutput = opticalFlowPass->runOpticalFlow(commandList, frameInfo, passInput);
		}
		{
			SCOPED_DRAW_EVENT(commandList, FrameGeneration);

			EFrameGenDispatchFlags dispatchFlags = EFrameGenDispatchFlags::NONE;
			if (renderOptions.bufferVisualization == EBufferVisualizationMode::FrameGenerationDebugView)
			{
				dispatchFlags |= EFrameGenDispatchFlags::DRAW_DEBUG_VIEW;
			}

			FrameGenPassInput passInput{
				.clearResourcePass          = clearResourcePass,
				.opticalFlowPassOutput      = &opticalFlowPassOutput,
				.camera                     = camera,
				.renderSizeX                = (int32)sceneWidth,
				.renderSizeY                = (int32)sceneHeight,
				.displaySizeX               = (int32)unscaledRenderWidth,
				.displaySizeY               = (int32)unscaledRenderHeight,
				.frameID                    = frameID,
				.dispatchFlags              = dispatchFlags,
				.backBufferTransferFunction = backbufferTransferFunction,
				.bReset                     = bResetOpticalFlowAccumulation,
				.minLuminance               = fMinLuminance,
				.maxLuminance               = fMaxLuminance,
				.sceneColorTexture          = frameGenInputColorTexture,
				.sceneColorSRV              = frameGenInputColorSRV,
				.sceneDepthTexture          = RT_sceneDepth.get(),
				.sceneDepthSRV              = sceneDepthSRV.get(),
				.motionVectorTexture        = RT_velocityMap.get(),
				.motionVectorSRV            = velocityMapSRV.get(),
			};
			frameGenPassOutput = frameGenPass->runFrameGeneration(commandList, frameInfo, passInput);
		}
	}

	// Buffer visualization
	if (renderOptions.bufferVisualization != EBufferVisualizationMode::None)
	{
		SCOPED_DRAW_EVENT(commandList, BufferVisualization);

		std::vector<TextureBarrierAuto> textureBarriers = {
			TextureBarrierAuto::toShaderResource(RT_gbuffers[0].get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_gbuffers[1].get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_sceneColor.get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_shadowMask.get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_indirectDiffuse.get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_indirectSpecular.get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_velocityMap.get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_visibilityBuffer.get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_barycentricCoord.get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_visGbuffers[0].get(), EBarrierSync::PIXEL_SHADING),
			TextureBarrierAuto::toShaderResource(RT_visGbuffers[1].get(), EBarrierSync::PIXEL_SHADING),
		};
		if (bRenderFrameGeneration)
		{
			textureBarriers.push_back(TextureBarrierAuto::toShaderResource(frameGenPassOutput.opticalFlowMotionVectorFieldTextures[0], EBarrierSync::PIXEL_SHADING));
			textureBarriers.push_back(TextureBarrierAuto::toShaderResource(frameGenPassOutput.opticalFlowMotionVectorFieldTextures[1], EBarrierSync::PIXEL_SHADING));
			textureBarriers.push_back(TextureBarrierAuto::toShaderResource(frameGenPassOutput.interpolatedFrameTexture, EBarrierSync::PIXEL_SHADING));
		}
		commandList->barrierAuto(0, nullptr, (uint32)textureBarriers.size(), textureBarriers.data(), 0, nullptr);

		BufferVisualizationInput sources{
			.renderTarget           = RT_finalSceneColor.get(),
			.mode                   = renderOptions.bufferVisualization,
			.textureWidth           = sceneWidth,
			.textureHeight          = sceneHeight,
			.sceneUniformCBV        = sceneUniformCBV,
			.gbuffer0SRV            = gbufferSRVs[0].get(),
			.gbuffer1SRV            = gbufferSRVs[1].get(),
			.sceneColorSRV          = sceneColorSRV.get(),
			.shadowMaskSRV          = shadowMaskSRV.get(),
			.indirectDiffuseSRV     = bRenderIndirectDiffuse ? indirectDiffuseSRV.get() : grey2DSRV.get(),
			.indirectSpecularSRV    = bRenderIndirectSpecular ? indirectSpecularSRV.get() : grey2DSRV.get(),
			.velocityMapSRV         = velocityMapSRV.get(),
			.visibilityBufferSRV    = visibilityBufferSRV.get(),
			.barycentricCoordSRV    = barycentricCoordSRV.get(),
			.visGbuffer0SRV         = visGbufferSRVs[0].get(),
			.visGbuffer1SRV         = visGbufferSRVs[1].get(),
			.opticalFlowVectorXSRV  = bRenderFrameGeneration ? frameGenPassOutput.opticalFlowMotionVectorFieldSRVs[0] : grey2DSRV.get(),
			.opticalFlowVectorYSRV  = bRenderFrameGeneration ? frameGenPassOutput.opticalFlowMotionVectorFieldSRVs[1] : grey2DSRV.get(),
			.opticalFlowVectorSizeX = bRenderFrameGeneration ? opticalFlowPassOutput.opticalFlowVectorSizeX : 1,
			.opticalFlowVectorSizeY = bRenderFrameGeneration ? opticalFlowPassOutput.opticalFlowVectorSizeY : 1,
			.interpolatedFrameSRV   = bRenderFrameGeneration ? frameGenPassOutput.interpolatedFrameSRV : grey2DSRV.get(),
		};

		bufferVisualization->renderVisualization(commandList, frameIndex, sources);
	}

	// Flush GPU before present work.
	immediateFlushCommandQueue(commandQueue, commandAllocator, commandList);
	resetCommandList(commandAllocator, commandList);

	// -----------------------------------------------------------------------
	// Internal rendering is done. Prepare to blit to the final render target.

	struct ScenePresentInfo
	{
		bool                bRealFrame;
		Texture*            colorTexture;
		ShaderResourceView* colorSRV;
	} scenePresentInfoArray[2];

	uint32 scenePresentCount = 0;
	if (bRenderFrameGeneration)
	{
		scenePresentInfoArray[scenePresentCount++] = ScenePresentInfo{
			.bRealFrame   = false,
			.colorTexture = RT_finalSceneColor.get(),
			.colorSRV     = finalSceneColorSRV.get(),
		};
	}
	scenePresentInfoArray[scenePresentCount++] = ScenePresentInfo{
		.bRealFrame   = true,
		.colorTexture = RT_finalSceneColor.get(),
		.colorSRV     = finalSceneColorSRV.get(),
	};

	finalBlitPass->resetBlitResources();

	for (uint32 presentIx = 0; presentIx < scenePresentCount; ++presentIx)
	{
		const ScenePresentInfo& presentInfo = scenePresentInfoArray[presentIx];

		// Debugging: Show only interpolated frame or real frame.
		//if (presentInfo.bRealFrame == true) continue;
		//if (presentInfo.bRealFrame == false) continue;

		// Set final render target.
		{
			SCOPED_DRAW_EVENT(commandList, SetFinalRenderTarget);

			const Viewport finalBlitViewport{
				.topLeftX = 0,
				.topLeftY = 0,
				.width    = static_cast<float>(finalBlitWidth),
				.height   = static_cast<float>(finalBlitHeight),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};
			const ScissorRect finalBlitScissorRect{
				.left   = 0,
				.top    = 0,
				.right  = finalBlitWidth,
				.bottom = finalBlitHeight,
			};
			commandList->rsSetViewport(finalBlitViewport);
			commandList->rsSetScissorRect(finalBlitScissorRect);

			TextureBarrierAuto barrier{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				finalBlitTarget, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			};
			commandList->barrierAuto(0, nullptr, 1, &barrier, 0, nullptr);

			commandList->omSetRenderTarget(finalBlitRTV, nullptr);
		}

		{
			SCOPED_DRAW_EVENT(commandList, FinalBlit);

			TextureBarrierAuto textureBarriers[] = {
				{
					EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
					presentInfo.colorTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
				},
			};
			commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

			FinalBlitPassInput passInput{
				.sceneUniformCBV    = sceneUniformCBV,
				.renderTarget       = renderOptions.finalRenderTarget,
				.finalSceneColorSRV = presentInfo.colorSRV,
			};
			finalBlitPass->renderFinalBlit(commandList, frameIndex, passInput);
		}

		// Dear Imgui: Record commands
		if (device->isHeadless() == false)
		{
			SCOPED_DRAW_EVENT(commandList, DearImgui);
		
			DescriptorHeap* imguiHeaps[] = { device->getDearImguiSRVHeap() };
			commandList->setDescriptorHeaps(1, imguiHeaps);
			device->renderDearImgui(commandList, swapchainBuffer);
		}

		//////////////////////////////////////////////////////////////////////////
		// Finalize

		if (bRenderToBackbuffer)
		{
			TextureBarrierAuto presentBarrier = {
				EBarrierSync::DRAW, EBarrierAccess::COMMON, EBarrierLayout::Present,
				swapchainBuffer, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
			};
			commandList->barrierAuto(0, nullptr, 1, &presentBarrier, 0, nullptr);
		}

		commandList->close();
		commandAllocator->markValid();
		commandQueue->executeCommandList(commandList, swapChain);
		{
			SCOPED_CPU_EVENT(WaitForGPU);
			device->flushCommandQueue();
		}

		if (bRenderToBackbuffer)
		{
			bool bVSync = scenePresentInfoArray[presentIx].bRealFrame;
			swapChain->present(bVSync);

			if (scenePresentInfoArray[presentIx].bRealFrame == false)
			{
				// #todo-fsr3-present: My measurement includes the time to present interpolated frame so can't use 0.5f * frameMS.
				// Not intuitive but it kinda works so leave it be?
				const float frameMS = avgRenderTime.getAverage();
				std::this_thread::sleep_for(std::chrono::milliseconds((uint32)(0.25f * frameMS)));
			}
		}

		if (presentIx != scenePresentCount - 1)
		{
			resetCommandList(commandAllocator, commandList);

			if (bRenderToBackbuffer)
			{
				swapChain->prepareBackbuffer();

				acquireSwapchainResources(swapchainBuffer, swapchainBufferRTV);

				finalBlitTarget    = swapchainBuffer;
				finalBlitRTV       = swapchainBufferRTV;
			}
		}
	}

	frameID += 1;
	avgRenderTime.push(renderOptions.prevRenderTime);

	prevScaledRenderResolutionX = sceneWidth;
	prevScaledRenderResolutionY = sceneHeight;

	// Deallocate memory, a bit messy
	commandList->executeDeferredDealloc();
	for (auto& cand : deferredCleanupList) delete cand.resource;
	deferredCleanupList.clear();
}

void SceneRenderer::recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight)
{
	renderResolutionX = sceneWidth;
	renderResolutionY = sceneHeight;
	if (prevScaledRenderResolutionX == 0 || prevScaledRenderResolutionY == 0)
	{
		prevScaledRenderResolutionX = sceneWidth;
		prevScaledRenderResolutionY = sceneHeight;
	}

	device->getDenoiserDevice()->recreateResources(sceneWidth, sceneHeight);

	auto& cleanupList = this->deferredCleanupList;
	auto cleanup = [&cleanupList](GPUResource* resource) {
		if (resource != nullptr)
		{
			cleanupList.push_back({ resource });
		}
	};

	cleanup(RT_visibilityBuffer.release());
	RT_visibilityBuffer = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			PF_visibilityBuffer,
			ETextureAccessFlags::RTV | ETextureAccessFlags::SRV,
			sceneWidth, sceneHeight, 1, 1, 0)));
	RT_visibilityBuffer->setDebugName(L"RT_VisibilityBuffer");
	visibilityBufferSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_visibilityBuffer.get(),
		ShaderResourceViewDesc{
			.format              = RT_visibilityBuffer->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_visibilityBuffer->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	visibilityBufferRTV = UniquePtr<RenderTargetView>(device->createRTV(RT_visibilityBuffer.get(),
		RenderTargetViewDesc{
			.format            = RT_visibilityBuffer->getCreateParams().format,
			.viewDimension     = ERTVDimension::Texture2D,
			.texture2D         = Texture2DRTVDesc{
				.mipSlice      = 0,
				.planeSlice    = 0,
			},
		}
	));

	cleanup(RT_barycentricCoord.release());
	RT_barycentricCoord = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			PF_barycentric,
			ETextureAccessFlags::UAV | ETextureAccessFlags::SRV,
			sceneWidth, sceneHeight, 1, 1, 0)));
	RT_barycentricCoord->setDebugName(L"RT_BarycentricCoord");
	barycentricCoordSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_barycentricCoord.get(),
		ShaderResourceViewDesc{
			.format              = RT_barycentricCoord->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_barycentricCoord->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	barycentricCoordUAV = UniquePtr<UnorderedAccessView>(device->createUAV(RT_barycentricCoord.get(),
		UnorderedAccessViewDesc{
			.format         = RT_barycentricCoord->getCreateParams().format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	for (uint32 i = 0; i < NUM_GBUFFERS; ++i)
	{
		cleanup(RT_visGbuffers[i].release());
		RT_visGbuffers[i] = UniquePtr<Texture>(device->createTexture(
			TextureCreateParams::texture2D(
				PF_gbuffers[i],
				ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
				sceneWidth, sceneHeight, 1, 1, 0)));
		std::wstring debugName = L"RT_VisGBuffer" + std::to_wstring(i);
		RT_visGbuffers[i]->setDebugName(debugName.c_str());

		visGbufferSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(RT_visGbuffers[i].get(),
			ShaderResourceViewDesc{
				.format              = PF_gbuffers[i],
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = RT_visGbuffers[i]->getCreateParams().mipLevels,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
		visGbufferUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(RT_visGbuffers[i].get(),
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
		PF_sceneDepth, ETextureAccessFlags::DSV, sceneWidth, sceneHeight,
		1, 1, 0).setOptimalClearDepth(getDeviceFarDepth());
	RT_sceneDepth = UniquePtr<Texture>(device->createTexture(sceneDepthDesc));
	RT_sceneDepth->setDebugName(L"RT_SceneDepth");

	sceneDepthDSV = UniquePtr<DepthStencilView>(device->createDSV(RT_sceneDepth.get(),
		DepthStencilViewDesc{
			.format        = PF_sceneDepthDSV,
			.viewDimension = EDSVDimension::Texture2D,
			.flags         = EDSVFlags::None,
			.texture2D     = Texture2DDSVDesc{ .mipSlice = 0 }
		}
	));
	sceneDepthSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_sceneDepth.get(),
		ShaderResourceViewDesc{
			.format              = PF_sceneDepthSRV,
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
			.format              = PF_sceneDepthSRV,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_prevSceneDepth->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));

	cleanup(RT_hiz.release());
	TextureCreateParams hizDesc = sceneDepthDesc;
	hizDesc.format = EPixelFormat::R32_FLOAT,
	hizDesc.accessFlags = ETextureAccessFlags::SRV | ETextureAccessFlags::UAV;
	hizDesc.mipLevels = fullMipCount(hizDesc.width, hizDesc.height);
	RT_hiz = UniquePtr<Texture>(device->createTexture(hizDesc));
	RT_hiz->setDebugName(L"RT_HiZ");
	hizSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_hiz.get(),
		ShaderResourceViewDesc{
			.format              = hizDesc.format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = hizDesc.mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	hizUAVs.initialize(hizDesc.mipLevels);
	for (uint16 mipLevel = 0; mipLevel < hizDesc.mipLevels; ++mipLevel)
	{
		hizUAVs[mipLevel] = UniquePtr<UnorderedAccessView>(device->createUAV(RT_hiz.get(),
			UnorderedAccessViewDesc{
				.format         = hizDesc.format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = mipLevel, .planeSlice = 0 },
			}
		));
	}

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
		gbufferUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(RT_gbuffers[i].get(),
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
	shadowMaskUAV = UniquePtr<UnorderedAccessView>(device->createUAV(RT_shadowMask.get(),
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
	indirectDiffuseUAV = UniquePtr<UnorderedAccessView>(device->createUAV(RT_indirectDiffuse.get(),
		UnorderedAccessViewDesc{
			.format         = RT_indirectDiffuse->getCreateParams().format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));

	cleanup(RT_indirectSpecular.release());
	cleanup(indirectSpecularTileCoordBuffer.release());
	cleanup(indirectSpecularTileCounterBuffer.release());
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
	indirectSpecularUAV = UniquePtr<UnorderedAccessView>(device->createUAV(RT_indirectSpecular.get(),
		UnorderedAccessViewDesc{
			.format         = RT_indirectSpecular->getCreateParams().format,
			.viewDimension  = EUAVDimension::Texture2D,
			.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
		}
	));
	uint32 tileCountX = (sceneWidth + 7) / 8, tileCountY = (sceneHeight + 7) / 8;
	indirectSpecularTileCoordBuffer = UniquePtr<Buffer>(device->createBuffer(
		BufferCreateParams{
			.sizeInBytes = sizeof(uint32) * tileCountX * tileCountY,
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::UAV,
		}
	));
	indirectSpecularTileCoordBufferUAV = UniquePtr<UnorderedAccessView>(device->createUAV(indirectSpecularTileCoordBuffer.get(),
		UnorderedAccessViewDesc{
			.format        = EPixelFormat::UNKNOWN,
			.viewDimension = EUAVDimension::Buffer,
			.buffer        = BufferUAVDesc{
				.firstElement         = 0,
				.numElements          = tileCountX * tileCountY,
				.structureByteStride  = sizeof(uint32),
				.counterOffsetInBytes = 0,
				.flags                = EBufferUAVFlags::None,
			}
		}
	));
	indirectSpecularTileCounterBuffer = UniquePtr<Buffer>(device->createBuffer(
		BufferCreateParams{
			.sizeInBytes = sizeof(uint32),
			.alignment   = 0,
			.accessFlags = EBufferAccessFlags::CPU_WRITE | EBufferAccessFlags::UAV,
		}
	));
	indirectSpecularTileCounterBufferUAV = UniquePtr<UnorderedAccessView>(device->createUAV(indirectSpecularTileCounterBuffer.get(),
		UnorderedAccessViewDesc{
			.format        = EPixelFormat::UNKNOWN,
			.viewDimension = EUAVDimension::Buffer,
			.buffer        = BufferUAVDesc{
				.firstElement         = 0,
				.numElements          = 1,
				.structureByteStride  = sizeof(uint32),
				.counterOffsetInBytes = 0,
				.flags                = EBufferUAVFlags::None,
			}
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
	pathTracingUAV = UniquePtr<UnorderedAccessView>(device->createUAV(RT_pathTracing.get(),
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

	cleanup(RT_finalSceneColor.release());
	RT_finalSceneColor = UniquePtr<Texture>(device->createTexture(
		TextureCreateParams::texture2D(
			PF_finalSceneColor,
			ETextureAccessFlags::RTV,
			sceneWidth, sceneHeight, 1, 1, 0)));
	RT_finalSceneColor->setDebugName(L"RT_FinalSceneColor");

	finalSceneColorSRV = UniquePtr<ShaderResourceView>(device->createSRV(RT_finalSceneColor.get(),
		ShaderResourceViewDesc{
			.format              = RT_finalSceneColor->getCreateParams().format,
			.viewDimension       = ESRVDimension::Texture2D,
			.texture2D           = Texture2DSRVDesc{
				.mostDetailedMip = 0,
				.mipLevels       = RT_finalSceneColor->getCreateParams().mipLevels,
				.planeSlice      = 0,
				.minLODClamp     = 0.0f,
			},
		}
	));
	finalSceneColorRTV = UniquePtr<RenderTargetView>(device->createRTV(RT_finalSceneColor.get(),
		RenderTargetViewDesc{
			.format            = RT_finalSceneColor->getCreateParams().format,
			.viewDimension     = ERTVDimension::Texture2D,
			.texture2D         = Texture2DRTVDesc{
				.mipSlice      = 0,
				.planeSlice    = 0,
			},
		}
	));
}

void SceneRenderer::enqueueCustomCommands(std::vector<RenderCommandList::CustomCommandType>&& inCommands)
{
	customCommands = inCommands;
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
	commandQueue->executeCommandList(commandList, nullptr);

	{
		SCOPED_CPU_EVENT(WaitForGPU);
		device->flushCommandQueue();
	}
}

void SceneRenderer::updateSceneUniform(
	RenderCommandList* commandList,
	uint32 frameIndex,
	const SceneProxy* scene,
	const Camera* camera,
	uint32 sceneWidth,
	uint32 sceneHeight)
{
	sceneUniformData.viewMatrix                    = camera->getViewMatrix();
	sceneUniformData.projMatrix                    = camera->getProjMatrix();
	sceneUniformData.viewProjMatrix                = camera->getViewProjMatrix();

	sceneUniformData.viewInvMatrix                 = camera->getViewInvMatrix();
	sceneUniformData.projInvMatrix                 = camera->getProjInvMatrix();
	sceneUniformData.viewProjInvMatrix             = camera->getViewProjInvMatrix();

	sceneUniformData.prevViewProjMatrix            = prevSceneUniformData.viewProjMatrix;
	sceneUniformData.prevViewProjInvMatrix         = prevSceneUniformData.viewProjInvMatrix;

	sceneUniformData.unscaledScreenResolution[0]   = (float)renderResolutionX;
	sceneUniformData.unscaledScreenResolution[1]   = (float)renderResolutionY;
	sceneUniformData.unscaledScreenResolution[2]   = 1.0f / (float)renderResolutionX;
	sceneUniformData.unscaledScreenResolution[3]   = 1.0f / (float)renderResolutionY;
	sceneUniformData.screenResolution[0]           = (float)sceneWidth;
	sceneUniformData.screenResolution[1]           = (float)sceneHeight;
	sceneUniformData.screenResolution[2]           = 1.0f / (float)sceneWidth;
	sceneUniformData.screenResolution[3]           = 1.0f / (float)sceneHeight;
	sceneUniformData.prevScreenResolution[0]       = (float)prevScaledRenderResolutionX;
	sceneUniformData.prevScreenResolution[1]       = (float)prevScaledRenderResolutionY;
	sceneUniformData.prevScreenResolution[2]       = 1.0f / (float)prevScaledRenderResolutionX;
	sceneUniformData.prevScreenResolution[3]       = 1.0f / (float)prevScaledRenderResolutionY;
	sceneUniformData.cameraFrustum                 = camera->getFrustum();
	sceneUniformData.cameraPosition                = camera->getPosition();
	sceneUniformData.sunDirection                  = scene->sun.direction;
	sceneUniformData.sunIlluminance                = scene->sun.illuminance;
	
	sceneUniformCBVs[frameIndex]->writeToGPU(commandList, &sceneUniformData, sizeof(sceneUniformData));

	memcpy_s(&prevSceneUniformData, sizeof(SceneUniform), &sceneUniformData, sizeof(SceneUniform));

	BufferBarrierAuto barrier{
		EBarrierSync::ALL, EBarrierAccess::CONSTANT_BUFFER, sceneUniformMemory.get(),
	};
	commandList->barrierAuto(1, &barrier, 0, nullptr, 0, nullptr);
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
	skyboxSRV = UniquePtr<ShaderResourceView>(device->createSRV(skyboxWithFallback,
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

void SceneRenderer::createFinalBlitRTV(RenderCommandList* commandList, const RendererOptions& renderOptions)
{
	commandList->enqueueDeferredDealloc(finalRenderTargetRTV.release(), true);

	if (renderOptions.renderToBackbuffer())
	{
		return;
	}
	Texture* texture = renderOptions.finalRenderTarget;

	finalRenderTargetRTV = UniquePtr<RenderTargetView>(device->createRTV(texture,
		RenderTargetViewDesc{
			.format            = texture->getCreateParams().format,
			.viewDimension     = ERTVDimension::Texture2D,
			.texture2D         = Texture2DRTVDesc{
				.mipSlice      = 0,
				.planeSlice    = 0,
			},
		}
	));
}
