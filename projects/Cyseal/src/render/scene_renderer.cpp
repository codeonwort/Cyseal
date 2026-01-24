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
#include "render/depth_prepass.h"
#include "render/decode_vis_buffer_pass.h"
#include "render/base_pass.h"
#include "render/hiz_pass.h"
#include "render/sky_pass.h"
#include "render/tone_mapping.h"
#include "render/buffer_visualization.h"
#include "render/store_history_pass.h"
#include "render/raytracing/ray_traced_shadows.h"
#include "render/raytracing/indirect_diffuse_pass.h"
#include "render/raytracing/indirect_specular_pass.h"
#include "render/pathtracing/path_tracing_pass.h"
#include "render/pathtracing/denoiser_plugin_pass.h"
#include "render/frame_gen_pass.h"

#include "util/profiling.h"

#define SCENE_UNIFORM_MEMORY_POOL_SIZE (64 * 1024) // 64 KiB
#define MAX_CULL_OPERATIONS            (2 * kMaxBasePassPermutation) // depth prepass + base pass

static const EPixelFormat PF_visibilityBuffer = EPixelFormat::R32_UINT;
static const EPixelFormat PF_barycentric = EPixelFormat::R16G16_FLOAT;
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

static uint32 fullMipCount(uint32 width, uint32 height)
{
	return static_cast<uint32>(floor(log2(std::max(width, height))) + 1);
}

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
		sceneRenderPasses.push_back(toneMapping = new(EMemoryTag::Renderer) ToneMapping);
		sceneRenderPasses.push_back(bufferVisualization = new(EMemoryTag::Renderer) BufferVisualization);
		sceneRenderPasses.push_back(pathTracingPass = new(EMemoryTag::Renderer) PathTracingPass);
		sceneRenderPasses.push_back(denoiserPluginPass = new(EMemoryTag::Renderer) DenoiserPluginPass);
		sceneRenderPasses.push_back(storeHistoryPass = new(EMemoryTag::Renderer) StoreHistoryPass);
		sceneRenderPasses.push_back(frameGenPass = new(EMemoryTag::Renderer) FrameGenPass);

		gpuScene->initialize(renderDevice);
		gpuCulling->initialize(renderDevice, MAX_CULL_OPERATIONS);
		bilateralBlur->initialize();
		rayTracedShadowsPass->initialize();
		depthPrepass->initialize(renderDevice, PF_visibilityBuffer);
		decodeVisBufferPass->initialize(renderDevice);
		basePass->initialize(renderDevice, PF_sceneColor, PF_gbuffers, NUM_GBUFFERS, PF_velocityMap);
		hizPass->initialize();
		skyPass->initialize(PF_sceneColor);
		indirectDiffusePass->initialize();
		indirectSpecularPass->initialize(renderDevice);
		toneMapping->initialize(renderDevice);
		bufferVisualization->initialize(renderDevice);
		pathTracingPass->initialize();
		denoiserPluginPass->initialize();
		storeHistoryPass->initialize(renderDevice);
		frameGenPass->initialize(renderDevice);
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
	bool bDoubleBuffering     = device->getCreateParams().bDoubleBuffering;
	
	auto swapChain            = device->getSwapChain();
	swapChain->prepareBackbuffer();

	uint32 swapchainIndex     = bDoubleBuffering ? swapChain->getNextBackbufferIndex() : swapChain->getCurrentBackbufferIndex();

	auto swapchainBuffer      = swapChain->getSwapchainBuffer(swapchainIndex);
	auto swapchainBufferRTV   = swapChain->getSwapchainBufferRTV(swapchainIndex);
	auto commandAllocator     = device->getCommandAllocator(swapchainIndex);
	auto commandList          = device->getCommandList(swapchainIndex);
	auto commandQueue         = device->getCommandQueue();

	if (bDoubleBuffering)
	{
		uint32 ix = swapChain->getCurrentBackbufferIndex();
		auto cmdAllocator = device->getCommandAllocator(ix);
		auto cmdList = device->getCommandList(ix);

		if (cmdAllocator->isValid())
		{
			commandQueue->executeCommandList(cmdList, swapChain);
		}
	}

	// #todo-renderer: Can be different due to resolution scaling
	const uint32 sceneWidth = swapChain->getBackbufferWidth();
	const uint32 sceneHeight = swapChain->getBackbufferHeight();

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
			.scene  = scene,
			.camera = camera,
		};
		gpuScene->renderGPUScene(commandList, swapchainIndex, passInput);
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
			.bIndirectDraw      = renderOptions.bEnableIndirectDraw,
			.bGPUCulling        = renderOptions.bEnableGPUCulling,
			.bVisibilityBuffer  = bRenderVisibilityBuffer,
			.sceneUniformBuffer = sceneUniformCBV,
			.gpuScene           = gpuScene,
			.gpuCulling         = gpuCulling,
		};
		depthPrepass->renderDepthPrepass(commandList, swapchainIndex, passInput);
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

		decodeVisBufferPass->decodeVisBuffer(commandList, swapchainIndex, passInput);
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
		rayTracedShadowsPass->renderRayTracedShadows(commandList, swapchainIndex, passInput);
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
			.bIndirectDraw      = renderOptions.bEnableIndirectDraw,
			.bGPUCulling        = renderOptions.bEnableGPUCulling,
			.sceneUniformBuffer = sceneUniformCBV,
			.gpuScene           = gpuScene,
			.gpuCulling         = gpuCulling,
			.shadowMaskSRV      = shadowMaskSRV.get(),
		};
		basePass->renderBasePass(commandList, swapchainIndex, passInput);
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
		storeHistoryPass->extractCurrent(commandList, swapchainIndex, passInput);
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
		hizPass->renderHiZ(commandList, swapchainIndex, passInput);
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
				denoiserPluginPass->blitTextures(commandList, swapchainIndex, passInput);
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

				denoiserPluginPass->executeDenoiser(commandList, RT_pathTracing.get());
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
		indirectDiffusePass->renderIndirectDiffuse(commandList, swapchainIndex, passInput);
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

		StoreHistoryPassResources historyResources = storeHistoryPass->getResources(swapchainIndex);
		
		IndirectSpecularInput passInput{
			.scene                   = scene,
			.mode                    = renderOptions.indirectSpecular,
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
			.gbuffer1SRV             = currentGBufferSRV0,
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
		indirectSpecularPass->renderIndirectSpecular(commandList, swapchainIndex, passInput);
	}

	// Tone mapping
	// final target: back buffer
	{
		SCOPED_DRAW_EVENT(commandList, ToneMapping);

		TextureBarrierAuto barriersBefore[] = {
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_sceneColor.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::DEPTH_STENCIL, EBarrierAccess::DEPTH_STENCIL_READ, EBarrierLayout::DepthStencilRead,
				RT_sceneDepth.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_indirectDiffuse.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_indirectSpecular.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_pathTracing.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
				swapchainBuffer, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			}
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		// #todo-renderer: Should not be here
		commandList->omSetRenderTarget(swapchainBufferRTV, nullptr);

		auto alternateSceneColorSRV = bRenderPathTracing ? pathTracingSRV.get() : sceneColorSRV.get();

		ToneMappingInput passInput{
			.viewport            = fullscreenViewport,
			.scissorRect         = fullscreenScissorRect,
			.sceneUniformCBV     = sceneUniformCBV,
			.sceneColorSRV       = alternateSceneColorSRV,
			.sceneDepthSRV       = sceneDepthSRV.get(),
			.gbuffer0SRV         = currentGBufferSRV0,
			.gbuffer1SRV         = currentGBufferSRV1,
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

		TextureBarrierAuto textureBarriers[] = {
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_gbuffers[0].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_gbuffers[1].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_sceneColor.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_shadowMask.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_indirectDiffuse.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_indirectSpecular.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_velocityMap.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_visibilityBuffer.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_barycentricCoord.get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_visGbuffers[0].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
			{
				EBarrierSync::PIXEL_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
				RT_visGbuffers[1].get(), BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
			},
		};
		commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

		BufferVisualizationInput sources{
			.mode                = renderOptions.bufferVisualization,
			.textureWidth        = sceneWidth,
			.textureHeight       = sceneHeight,
			.gbuffer0SRV         = gbufferSRVs[0].get(),
			.gbuffer1SRV         = gbufferSRVs[1].get(),
			.sceneColorSRV       = sceneColorSRV.get(),
			.shadowMaskSRV       = shadowMaskSRV.get(),
			.indirectDiffuseSRV  = bRenderIndirectDiffuse ? indirectDiffuseSRV.get() : grey2DSRV.get(),
			.indirectSpecularSRV = bRenderIndirectSpecular ? indirectSpecularSRV.get() : grey2DSRV.get(),
			.velocityMapSRV      = velocityMapSRV.get(),
			.visibilityBufferSRV = visibilityBufferSRV.get(),
			.barycentricCoordSRV = barycentricCoordSRV.get(),
			.visGbuffer0SRV      = visGbufferSRVs[0].get(),
			.visGbuffer1SRV      = visGbufferSRVs[1].get(),
		};

		bufferVisualization->renderVisualization(commandList, swapchainIndex, sources);
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

		storeHistoryPass->copyCurrentToPrev(commandList, swapchainIndex);
	}

	//////////////////////////////////////////////////////////////////////////
	// Dear Imgui: Record commands

	{
		SCOPED_DRAW_EVENT(commandList, DearImgui);

		DescriptorHeap* imguiHeaps[] = { device->getDearImguiSRVHeap() };
		commandList->setDescriptorHeaps(1, imguiHeaps);
		device->renderDearImgui(commandList, swapchainBuffer);
	}

	//////////////////////////////////////////////////////////////////////////
	// Finalize

	TextureBarrierAuto presentBarrier = {
		EBarrierSync::DRAW, EBarrierAccess::COMMON, EBarrierLayout::Present,
		swapchainBuffer, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
	};
	commandList->barrierAuto(0, nullptr, 1, &presentBarrier, 0, nullptr);

	commandList->close();
	commandAllocator->markValid();

	if (!bDoubleBuffering)
	{
		commandQueue->executeCommandList(commandList, swapChain);
	}

	swapChain->present();

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
			.accessFlags = EBufferAccessFlags::COPY_SRC | EBufferAccessFlags::UAV,
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
	uint32 swapchainIndex,
	const SceneProxy* scene,
	const Camera* camera)
{
	const float sceneWidth = (float)device->getSwapChain()->getBackbufferWidth();
	const float sceneHeight = (float)device->getSwapChain()->getBackbufferHeight();

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
