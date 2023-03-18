#include "app.h"
#include "core/core_minimal.h"
#include "rhi/render_command.h"
#include "rhi/gpu_resource.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/render_device_capabilities.h"
#include "rhi/texture_manager.h"
#include "render/material.h"
#include "render/static_mesh.h"
#include "geometry/primitive.h"
#include "geometry/procedural.h"
#include "loader/image_loader.h"
#include "world/gpu_resource_asset.h"
#include "util/profiling.h"

#include "imgui.h"

#include <algorithm>
#include <array>

/* -------------------------------------------------------
					CONFIGURATION
--------------------------------------------------------*/
// 0: DX12 + Standard renderer
// 1: Vulkan + Null renderer
// 2: Vulkan + Standard renderer (WIP)
#define RENDERER_PRESET 0

#if RENDERER_PRESET == 0
	#define RAW_API          ERenderDeviceRawAPI::DirectX12
	#define RENDERER_TYPE    ERendererType::Standard
#elif RENDERER_PRESET == 1
	#define RAW_API          ERenderDeviceRawAPI::Vulkan
	#define RENDERER_TYPE    ERendererType::Null
#elif RENDERER_PRESET == 2
	#define RAW_API          ERenderDeviceRawAPI::Vulkan
	#define RENDERER_TYPE    ERendererType::Standard
#endif
#define WINDOW_TYPE          EWindowType::WINDOWED

#define DOUBLE_BUFFERING     true
#define RAYTRACING_TIER      ERaytracingTier::MaxTier

#define CAMERA_POSITION      vec3(0.0f, 0.0f, 30.0f)
#define CAMERA_LOOKAT        vec3(0.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

#define CRUMPLED_WORLD       0

#define SUN_DIRECTION        normalize(vec3(-1.0f, -1.0f, -1.0f))
#define SUN_ILLUMINANCE      (2.0f * vec3(1.0f, 1.0f, 1.0f))

/* -------------------------------------------------------
					APPLICATION
--------------------------------------------------------*/
CysealEngine cysealEngine;

DEFINE_LOG_CATEGORY_STATIC(LogApplication);

bool TestApplication::onInitialize()
{
	CysealEngineCreateParams engineInit;
	engineInit.renderDevice.rawAPI             = RAW_API;
	engineInit.renderDevice.nativeWindowHandle = getHWND();
	engineInit.renderDevice.windowType         = WINDOW_TYPE;
	engineInit.renderDevice.windowWidth        = getWindowWidth();
	engineInit.renderDevice.windowHeight       = getWindowHeight();
	engineInit.renderDevice.raytracingTier     = RAYTRACING_TIER;
	engineInit.renderDevice.bDoubleBuffering   = DOUBLE_BUFFERING;
	engineInit.rendererType                    = RENDERER_TYPE;

	cysealEngine.startup(engineInit);

	createResources();

	camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

	return true;
}

void TestApplication::onTick(float deltaSeconds)
{
	{
		SCOPED_CPU_EVENT(WorldLogic);

		wchar_t buf[256];
		float newFPS = 1.0f / deltaSeconds;
		framesPerSecond += 0.05f * (newFPS - framesPerSecond);
		swprintf_s(buf, L"Hello World / FPS: %.2f", framesPerSecond);
		setWindowTitle(std::wstring(buf));

		// #todo-app: Control camera by user input
		// Animate camera to see if raytracing is actually working in world space.
		{
			static float elapsed = 0.0f;
			elapsed += 0.5f * deltaSeconds;

			if (appState.bRotateCamera)
			{
				float theta = 3.0f * elapsed;
				vec3 posDelta = vec3(cosf(theta), 0.0f, sin(theta));
				camera.lookAt(CAMERA_POSITION, CAMERA_POSITION + posDelta, CAMERA_UP);
			}
			else
			{
				vec3 posDelta = vec3(5.0f * sinf(elapsed), 0.0f, 3.0f * cosf(elapsed));
				camera.lookAt(CAMERA_POSITION + posDelta, CAMERA_LOOKAT + posDelta, CAMERA_UP);
			}
			//ground->getTransform().setScale(1.0f + 0.2f * cosf(elapsed));
			ground->setRotation(vec3(0.0f, 1.0f, 0.0f), elapsed * 30.0f);
		}

		// Animate balls to see if update of BLAS instance transforms is going well.
		meshSplatting.tick(deltaSeconds);
	}

	// #todo: Move rendering loop to engine
	{
		SCOPED_CPU_EVENT(ExecuteRenderer);

		if (bViewportNeedsResize)
		{
			cysealEngine.getRenderDevice()->recreateSwapChain(getHWND(), newViewportWidth, newViewportHeight);
			cysealEngine.getRenderer()->recreateSceneTextures(newViewportWidth, newViewportHeight);
			bViewportNeedsResize = false;
		}

		cysealEngine.beginImguiNewFrame();
		{
			//ImGui::ShowDemoWindow(0);

			ImGui::Begin("Rendering options");
			ImGui::Checkbox("Base Pass - Indirect Draw", &appState.rendererOptions.bEnableIndirectDraw);
			if (!appState.rendererOptions.bEnableIndirectDraw)
			{
				ImGui::BeginDisabled();
			}
			ImGui::Checkbox("Base Pass - GPU Culling", &appState.rendererOptions.bEnableGPUCulling);
			if (!appState.rendererOptions.bEnableIndirectDraw)
			{
				ImGui::EndDisabled();
			}
			ImGui::Checkbox("Ray Traced Reflections", &appState.rendererOptions.bEnableRayTracedReflections);

			ImGui::Checkbox("Rotate Camera", &appState.bRotateCamera);
			//ImGui::SliderFloat("float", &sliderValue, 0.0f, 1.0f);
			ImGui::End();
		}
		cysealEngine.renderImgui();

		SceneProxy* sceneProxy = scene.createProxy();

		cysealEngine.getRenderer()->render(sceneProxy, &camera, appState.rendererOptions);

		delete sceneProxy;
	}
}

void TestApplication::onTerminate()
{
	destroyResources();

	cysealEngine.shutdown();
}

void TestApplication::onWindowResize(uint32 newWidth, uint32 newHeight)
{
	bViewportNeedsResize = true;
	newViewportWidth = newWidth;
	newViewportHeight = newHeight;

	camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);
}

void TestApplication::createResources()
{
	ImageLoader imageLoader;
	ImageLoadData* imageBlob = imageLoader.load(L"bee.png");
	if (imageBlob == nullptr)
	{
		imageBlob = new ImageLoadData;

		// Fill random image
		imageBlob->numComponents = 4;
		imageBlob->width = 256;
		imageBlob->height = 256;
		imageBlob->length = 256 * 256 * 4;
		imageBlob->buffer = new uint8[imageBlob->length];
		int32 p = 0;
		for (int32 y = 0; y < 256; ++y)
		{
			for (int32 x = 0; x < 256; ++x)
			{
				imageBlob->buffer[p] = x ^ y;
				imageBlob->buffer[p+1] = x ^ y;
				imageBlob->buffer[p+2] = x ^ y;
				imageBlob->buffer[p+3] = 0xff;
				p += 4;
			}
		}
	}

	std::shared_ptr<TextureAsset> albedoTexture = std::make_shared<TextureAsset>();
	ENQUEUE_RENDER_COMMAND(CreateTexture)(
		[albedoTexture, imageBlob](RenderCommandList& commandList)
		{
			TextureCreateParams params = TextureCreateParams::texture2D(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
				imageBlob->width, imageBlob->height, 1);

			Texture* texture = gRenderDevice->createTexture(params);
			texture->uploadData(commandList, imageBlob->buffer, imageBlob->getRowPitch(), imageBlob->getSlicePitch());
			texture->setDebugName(TEXT("Texture_albedoTest"));

			albedoTexture->setGPUResource(std::shared_ptr<Texture>(texture));

			commandList.enqueueDeferredDealloc(imageBlob);
		}
	);

	meshSplatting.createResources(
		MeshSplatting::CreateParams{
			.center = vec3(0.0f, -4.0f, 0.0f),
			.radius = 16.0f,
			.height = 20.0f,
			.numLoop = 2,
			.numMeshes = 32
		}
	);
	for (StaticMesh* sm : meshSplatting.getStaticMeshes())
	{
		scene.addStaticMesh(sm);
	}

	// Ground
	{
		Geometry* planeGeometry = new Geometry;
#if CRUMPLED_WORLD
		ProceduralGeometry::crumpledPaper(*planeGeometry,
			100.0f, 100.0f, 16, 16,
			2.0f,
			ProceduralGeometry::EPlaneNormal::Y);
#else
		ProceduralGeometry::plane(*planeGeometry,
			100.0f, 100.0f, 2, 2,
			ProceduralGeometry::EPlaneNormal::Y);
#endif
		AABB localBounds = planeGeometry->localBounds;

		std::shared_ptr<VertexBufferAsset> positionBufferAsset = std::make_shared<VertexBufferAsset>();
		std::shared_ptr<VertexBufferAsset> nonPositionBufferAsset = std::make_shared<VertexBufferAsset>();
		std::shared_ptr<IndexBufferAsset> indexBufferAsset = std::make_shared<IndexBufferAsset>();
		ENQUEUE_RENDER_COMMAND(UploadGroundMesh)(
			[planeGeometry, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset](RenderCommandList& commandList)
			{
				auto positionBuffer = gVertexBufferPool->suballocate(planeGeometry->getPositionBufferTotalBytes());
				auto nonPositionBuffer = gVertexBufferPool->suballocate(planeGeometry->getNonPositionBufferTotalBytes());
				auto indexBuffer = gIndexBufferPool->suballocate(planeGeometry->getIndexBufferTotalBytes(), planeGeometry->getIndexFormat());

				positionBuffer->updateData(&commandList, planeGeometry->getPositionBlob(), planeGeometry->getPositionStride());
				nonPositionBuffer->updateData(&commandList, planeGeometry->getNonPositionBlob(), planeGeometry->getNonPositionStride());
				indexBuffer->updateData(&commandList, planeGeometry->getIndexBlob(), planeGeometry->getIndexFormat());

				positionBufferAsset->setGPUResource(std::shared_ptr<VertexBuffer>(positionBuffer));
				nonPositionBufferAsset->setGPUResource(std::shared_ptr<VertexBuffer>(nonPositionBuffer));
				indexBufferAsset->setGPUResource(std::shared_ptr<IndexBuffer>(indexBuffer));

				commandList.enqueueDeferredDealloc(planeGeometry);
			}
		);

		auto material = std::make_shared<Material>();
		material->albedoMultiplier[0] = 1.0f;
		material->albedoMultiplier[1] = 1.0f;
		material->albedoMultiplier[2] = 1.0f;
		material->albedoTexture = gTextureManager->getSystemTextureGrey2D();
		material->roughness = 0.0f;

		ground = new StaticMesh;
		ground->addSection(0, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset, material, localBounds);
		ground->setPosition(vec3(0.0f, -10.0f, 0.0f));

		scene.addStaticMesh(ground);
	}

	// wallA
	{
		Geometry* planeGeometry = new Geometry;
#if CRUMPLED_WORLD
		ProceduralGeometry::crumpledPaper(*planeGeometry,
			50.0f, 50.0f, 16, 16,
			1.0f,
			ProceduralGeometry::EPlaneNormal::X);
#else
		ProceduralGeometry::plane(*planeGeometry,
			50.0f, 50.0f, 2, 2,
			ProceduralGeometry::EPlaneNormal::X);
#endif
		AABB localBounds = planeGeometry->localBounds;

		std::shared_ptr<VertexBufferAsset> positionBufferAsset = std::make_shared<VertexBufferAsset>();
		std::shared_ptr<VertexBufferAsset> nonPositionBufferAsset = std::make_shared<VertexBufferAsset>();
		std::shared_ptr<IndexBufferAsset> indexBufferAsset = std::make_shared<IndexBufferAsset>();
		ENQUEUE_RENDER_COMMAND(UploadGroundMesh)(
			[planeGeometry, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset](RenderCommandList& commandList)
			{
				auto positionBuffer = gVertexBufferPool->suballocate(planeGeometry->getPositionBufferTotalBytes());
				auto nonPositionBuffer = gVertexBufferPool->suballocate(planeGeometry->getNonPositionBufferTotalBytes());
				auto indexBuffer = gIndexBufferPool->suballocate(planeGeometry->getIndexBufferTotalBytes(), planeGeometry->getIndexFormat());

				positionBuffer->updateData(&commandList, planeGeometry->getPositionBlob(), planeGeometry->getPositionStride());
				nonPositionBuffer->updateData(&commandList, planeGeometry->getNonPositionBlob(), planeGeometry->getNonPositionStride());
				indexBuffer->updateData(&commandList, planeGeometry->getIndexBlob(), planeGeometry->getIndexFormat());

				positionBufferAsset->setGPUResource(std::shared_ptr<VertexBuffer>(positionBuffer));
				nonPositionBufferAsset->setGPUResource(std::shared_ptr<VertexBuffer>(nonPositionBuffer));
				indexBufferAsset->setGPUResource(std::shared_ptr<IndexBuffer>(indexBuffer));

				commandList.enqueueDeferredDealloc(planeGeometry);
			}
		);

		auto material = std::make_shared<Material>();
		material->albedoMultiplier[0] = 1.0f;
		material->albedoMultiplier[1] = 1.0f;
		material->albedoMultiplier[2] = 1.0f;
		material->albedoTexture = albedoTexture;
		material->roughness = 0.0f;

		wallA = new StaticMesh;
		wallA->addSection(0, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset, material, localBounds);
		wallA->setPosition(vec3(-25.0f, 0.0f, 0.0f));
		wallA->setRotation(vec3(0.0f, 0.0f, 1.0f), -10.0f);

		scene.addStaticMesh(wallA);
	}

	// Skybox
	std::wstring skyboxFilepaths[] = {
		L"skybox_Footballfield/posx.jpg", L"skybox_Footballfield/negx.jpg",
		L"skybox_Footballfield/posy.jpg", L"skybox_Footballfield/negy.jpg",
		L"skybox_Footballfield/posz.jpg", L"skybox_Footballfield/negz.jpg",
	};
	std::array<ImageLoadData*, 6> skyboxBlobs = { nullptr, };
	bool bValidSkyboxBlobs = true;
	for (uint32 i = 0; i < 6; ++i)
	{
		skyboxBlobs[i] = imageLoader.load(skyboxFilepaths[i]);
		bValidSkyboxBlobs = bValidSkyboxBlobs
			&& (skyboxBlobs[i] != nullptr)
			&& (skyboxBlobs[i]->width == skyboxBlobs[0]->width)
			&& (skyboxBlobs[i]->height == skyboxBlobs[0]->height);
	}
	if (bValidSkyboxBlobs)
	{
		std::shared_ptr<TextureAsset> skyboxTexture = std::make_shared<TextureAsset>();
		ENQUEUE_RENDER_COMMAND(CreateSkybox)(
			[skyboxTexture, skyboxBlobs](RenderCommandList& commandList)
			{
				TextureCreateParams params = TextureCreateParams::textureCube(
					EPixelFormat::R8G8B8A8_UNORM,
					ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
					skyboxBlobs[0]->width, skyboxBlobs[0]->height, 1);

				Texture* texture = gRenderDevice->createTexture(params);
				for (uint32 i = 0; i < 6; ++i)
				{
					texture->uploadData(commandList,
						skyboxBlobs[i]->buffer,
						skyboxBlobs[i]->getRowPitch(),
						skyboxBlobs[i]->getSlicePitch(),
						i);
				}
				texture->setDebugName(TEXT("Texture_skybox"));

				skyboxTexture->setGPUResource(std::shared_ptr<Texture>(texture));

				for (ImageLoadData* imageBlob : skyboxBlobs)
				{
					commandList.enqueueDeferredDealloc(imageBlob);
				}
			}
		);
		scene.skyboxTexture = skyboxTexture;
	}

	scene.sun.direction = SUN_DIRECTION;
	scene.sun.illuminance = SUN_ILLUMINANCE;
}

void TestApplication::destroyResources()
{
	meshSplatting.destroyResources();
	delete ground;
	delete wallA;

	scene.skyboxTexture.reset();
}
