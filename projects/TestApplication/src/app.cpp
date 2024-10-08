#include "app.h"
#include "core/core_minimal.h"
#include "core/smart_pointer.h"
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
#include "loader/pbrt_loader.h"
#include "loader/ply_loader.h"
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
#define RENDERER_PRESET      0

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

#define CAMERA_POSITION      vec3(50.0f, 0.0f, 30.0f)
#define CAMERA_LOOKAT        vec3(50.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

#define CRUMPLED_WORLD       0

#define SUN_DIRECTION        normalize(vec3(-1.0f, -1.0f, -1.0f))
#define SUN_ILLUMINANCE      (2.0f * vec3(1.0f, 1.0f, 1.0f))

// #todo: DX12 + Null renderer -> SystemTexture2DWhite not free'd?
#define LOAD_PBRT_FILE       1
// living-room contains an invalid leaf texture only for pbrt format :/
// It's tungsten and mitsuba versions are fine.
//#define PBRT_FILEPATH        L"external/pbrt4/living-room/scene-v4.pbrt"
#define PBRT_FILEPATH        L"external/pbrt4/bedroom/scene-v4.pbrt"

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

	appState.cameraLocation = CAMERA_POSITION;
	appState.cameraRotationY = -90.0f;

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

		// Control camera by user input.
		bool bCameraHasMoved = false;
		{
			float moveX = ImGui::IsKeyDown(ImGuiKey_A) ? -1.0f : ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f;
			float moveZ = ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : ImGui::IsKeyDown(ImGuiKey_S) ? -1.0f : 0.0f;
			float rotateY = ImGui::IsKeyDown(ImGuiKey_Q) ? -1.0f : ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f;

			bCameraHasMoved = (moveX != 0.0f || moveZ != 0.0f || rotateY != 0.0f);

			appState.cameraRotationY += rotateY * deltaSeconds * 45.0f;
			float theta = Cymath::radians(appState.cameraRotationY);
			float theta2 = Cymath::radians(appState.cameraRotationY + 90.0f);

			vec3 vForward = vec3(Cymath::cos(theta), 0.0f, Cymath::sin(theta));
			vec3 vRight = vec3(Cymath::cos(theta2), 0.0f, Cymath::sin(theta2));

			appState.cameraLocation += vForward * moveZ * deltaSeconds * 10.0f;
			appState.cameraLocation += vRight * moveX * deltaSeconds * 10.0f;

			camera.lookAt(appState.cameraLocation, appState.cameraLocation + vForward, CAMERA_UP);
		}
		appState.rendererOptions.bCameraHasMoved = bCameraHasMoved;

		if (bCameraHasMoved)
		{
			appState.pathTracingNumFrames = 0;
		}
		else if (appState.rendererOptions.bEnablePathTracing)
		{
			appState.pathTracingNumFrames += 1;
		}

		// Animate meshes.
		if (!appState.rendererOptions.bEnablePathTracing)
		{
			static float elapsed = 0.0f;
			elapsed += deltaSeconds;

			//ground->getTransform().setScale(1.0f + 0.2f * cosf(elapsed));
			ground->setRotation(vec3(0.0f, 1.0f, 0.0f), elapsed * 15.0f);

			// Animate balls to see if update of BLAS instance transforms is going well.
			meshSplatting.tick(deltaSeconds);
		}
	}

	// #todo: Move rendering loop to engine
	{
		SCOPED_CPU_EVENT(ExecuteRenderer);

		if (bViewportNeedsResize)
		{
			cysealEngine.setRenderResolution(newViewportWidth, newViewportHeight);
			bViewportNeedsResize = false;
		}

		cysealEngine.beginImguiNewFrame();
		{
			//ImGui::ShowDemoWindow(0);

			ImGui::Begin("Cyseal");

			ImGui::SeparatorText("Rendering options");
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

			ImGui::Combo("Visualization Mode", &appState.selectedBufferVisualizationMode, getBufferVisualizationModeNames(), (int32)EBufferVisualizationMode::Count);
			appState.rendererOptions.bufferVisualization = (EBufferVisualizationMode)appState.selectedBufferVisualizationMode;

			ImGui::SeparatorText("Path Tracing");
			ImGui::Checkbox("Enable", &appState.rendererOptions.bEnablePathTracing);
			ImGui::Text("Frames: %u", appState.pathTracingNumFrames);

			ImGui::SeparatorText("Control");
			ImGui::Text("WASD : move camera");
			ImGui::Text("QE   : rotate camera");
			
			ImGui::End();
		}
		cysealEngine.renderImgui();

		SceneProxy* sceneProxy = scene.createProxy();

		cysealEngine.renderScene(sceneProxy, &camera, appState.rendererOptions);

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

	SharedPtr<TextureAsset> albedoTexture = makeShared<TextureAsset>();
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

			albedoTexture->setGPUResource(SharedPtr<Texture>(texture));

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

		SharedPtr<VertexBufferAsset> positionBufferAsset = makeShared<VertexBufferAsset>();
		SharedPtr<VertexBufferAsset> nonPositionBufferAsset = makeShared<VertexBufferAsset>();
		SharedPtr<IndexBufferAsset> indexBufferAsset = makeShared<IndexBufferAsset>();
		ENQUEUE_RENDER_COMMAND(UploadGroundMesh)(
			[planeGeometry, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset](RenderCommandList& commandList)
			{
				auto positionBuffer = gVertexBufferPool->suballocate(planeGeometry->getPositionBufferTotalBytes());
				auto nonPositionBuffer = gVertexBufferPool->suballocate(planeGeometry->getNonPositionBufferTotalBytes());
				auto indexBuffer = gIndexBufferPool->suballocate(planeGeometry->getIndexBufferTotalBytes(), planeGeometry->getIndexFormat());

				positionBuffer->updateData(&commandList, planeGeometry->getPositionBlob(), planeGeometry->getPositionStride());
				nonPositionBuffer->updateData(&commandList, planeGeometry->getNonPositionBlob(), planeGeometry->getNonPositionStride());
				indexBuffer->updateData(&commandList, planeGeometry->getIndexBlob(), planeGeometry->getIndexFormat());

				positionBufferAsset->setGPUResource(SharedPtr<VertexBuffer>(positionBuffer));
				nonPositionBufferAsset->setGPUResource(SharedPtr<VertexBuffer>(nonPositionBuffer));
				indexBufferAsset->setGPUResource(SharedPtr<IndexBuffer>(indexBuffer));

				commandList.enqueueDeferredDealloc(planeGeometry);
			}
		);

		auto material = makeShared<Material>();
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

		SharedPtr<VertexBufferAsset> positionBufferAsset = makeShared<VertexBufferAsset>();
		SharedPtr<VertexBufferAsset> nonPositionBufferAsset = makeShared<VertexBufferAsset>();
		SharedPtr<IndexBufferAsset> indexBufferAsset = makeShared<IndexBufferAsset>();
		ENQUEUE_RENDER_COMMAND(UploadGroundMesh)(
			[planeGeometry, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset](RenderCommandList& commandList)
			{
				auto positionBuffer = gVertexBufferPool->suballocate(planeGeometry->getPositionBufferTotalBytes());
				auto nonPositionBuffer = gVertexBufferPool->suballocate(planeGeometry->getNonPositionBufferTotalBytes());
				auto indexBuffer = gIndexBufferPool->suballocate(planeGeometry->getIndexBufferTotalBytes(), planeGeometry->getIndexFormat());

				positionBuffer->updateData(&commandList, planeGeometry->getPositionBlob(), planeGeometry->getPositionStride());
				nonPositionBuffer->updateData(&commandList, planeGeometry->getNonPositionBlob(), planeGeometry->getNonPositionStride());
				indexBuffer->updateData(&commandList, planeGeometry->getIndexBlob(), planeGeometry->getIndexFormat());

				positionBufferAsset->setGPUResource(SharedPtr<VertexBuffer>(positionBuffer));
				nonPositionBufferAsset->setGPUResource(SharedPtr<VertexBuffer>(nonPositionBuffer));
				indexBufferAsset->setGPUResource(SharedPtr<IndexBuffer>(indexBuffer));

				commandList.enqueueDeferredDealloc(planeGeometry);
			}
		);

		auto material = makeShared<Material>();
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
		SharedPtr<TextureAsset> skyboxTexture = makeShared<TextureAsset>();
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

				skyboxTexture->setGPUResource(SharedPtr<Texture>(texture));

				for (ImageLoadData* imageBlob : skyboxBlobs)
				{
					commandList.enqueueDeferredDealloc(imageBlob);
				}
			}
		);
		scene.skyboxTexture = skyboxTexture;
	}

	// #todo-pathtracing: Something messed up if the pbrt mesh is added prior to other meshes.
	// Currently only pbrt mesh contains multiple mesh sections.
	// It's highly suspicious that mesh index, gpu scene item index, and material index are out of sync.
	if (LOAD_PBRT_FILE)
	{
		PBRT4Loader pbrtLoader;
		PBRT4Scene* pbrtScene = pbrtLoader.loadFromFile(PBRT_FILEPATH);
		if (pbrtScene != nullptr)
		{
			const size_t numTriangleMeshes = pbrtScene->triangleMeshes.size();
			const size_t numPbrtMeshes = pbrtScene->plyMeshes.size();
			const size_t totalSubMeshes = numTriangleMeshes + numPbrtMeshes;
			std::vector<Geometry*> pbrtGeometries(totalSubMeshes, nullptr);
			std::vector<SharedPtr<VertexBufferAsset>> positionBufferAssets(totalSubMeshes, nullptr);
			std::vector<SharedPtr<VertexBufferAsset>> nonPositionBufferAssets(totalSubMeshes, nullptr);
			std::vector<SharedPtr<IndexBufferAsset>> indexBufferAssets(totalSubMeshes, nullptr);
			std::vector<SharedPtr<Material>> subMaterials(totalSubMeshes, nullptr);
			for (size_t i = 0; i < totalSubMeshes; ++i)
			{
				Geometry* pbrtGeometry = pbrtGeometries[i] = new Geometry;

				if (i < numTriangleMeshes)
				{
					PBRT4TriangleMesh& triMesh = pbrtScene->triangleMeshes[i];

					pbrtGeometry->positions = std::move(triMesh.positionBuffer);
					pbrtGeometry->normals = std::move(triMesh.normalBuffer);
					pbrtGeometry->texcoords = std::move(triMesh.texcoordBuffer);
					pbrtGeometry->indices = std::move(triMesh.indexBuffer);
					pbrtGeometry->recalculateNormals();
					pbrtGeometry->finalize();

					subMaterials[i] = triMesh.material;
				}
				else
				{
					PLYMesh* plyMesh = pbrtScene->plyMeshes[i - numTriangleMeshes];

					pbrtGeometry->positions = std::move(plyMesh->positionBuffer);
					pbrtGeometry->normals = std::move(plyMesh->normalBuffer);
					pbrtGeometry->texcoords = std::move(plyMesh->texcoordBuffer);
					pbrtGeometry->indices = std::move(plyMesh->indexBuffer);
					pbrtGeometry->recalculateNormals();
					pbrtGeometry->finalize();

					subMaterials[i] = plyMesh->material;
				}

				positionBufferAssets[i] = makeShared<VertexBufferAsset>();
				nonPositionBufferAssets[i] = makeShared<VertexBufferAsset>();
				indexBufferAssets[i] = makeShared<IndexBufferAsset>();
			}

			ENQUEUE_RENDER_COMMAND(UploadPBRTSubMeshes)(
				[pbrtScene, pbrtGeometries, positionBufferAssets, nonPositionBufferAssets, indexBufferAssets](RenderCommandList& commandList)
				{
					for (size_t i = 0; i < pbrtGeometries.size(); ++i)
					{
						Geometry* geom = pbrtGeometries[i];
						auto positionBuffer = gVertexBufferPool->suballocate(geom->getPositionBufferTotalBytes());
						auto nonPositionBuffer = gVertexBufferPool->suballocate(geom->getNonPositionBufferTotalBytes());
						auto indexBuffer = gIndexBufferPool->suballocate(geom->getIndexBufferTotalBytes(), geom->getIndexFormat());

						positionBuffer->updateData(&commandList, geom->getPositionBlob(), geom->getPositionStride());
						nonPositionBuffer->updateData(&commandList, geom->getNonPositionBlob(), geom->getNonPositionStride());
						indexBuffer->updateData(&commandList, geom->getIndexBlob(), geom->getIndexFormat());

						positionBufferAssets[i]->setGPUResource(SharedPtr<VertexBuffer>(positionBuffer));
						nonPositionBufferAssets[i]->setGPUResource(SharedPtr<VertexBuffer>(nonPositionBuffer));
						indexBufferAssets[i]->setGPUResource(SharedPtr<IndexBuffer>(indexBuffer));

						commandList.enqueueDeferredDealloc(geom);
					}
					commandList.enqueueDeferredDealloc(pbrtScene);
				}
			);

			auto fallbackMaterial = makeShared<Material>();
			fallbackMaterial->albedoMultiplier[0] = 1.0f;
			fallbackMaterial->albedoMultiplier[1] = 1.0f;
			fallbackMaterial->albedoMultiplier[2] = 1.0f;
			fallbackMaterial->albedoTexture = gTextureManager->getSystemTextureGrey2D();
			fallbackMaterial->roughness = 1.0f;

			pbrtMesh = new StaticMesh;
			for (size_t i = 0; i < totalSubMeshes; ++i)
			{
				AABB localBounds = pbrtGeometries[i]->localBounds;
				auto material = (subMaterials[i] != nullptr) ? subMaterials[i] : fallbackMaterial;
				pbrtMesh->addSection(
					0,
					positionBufferAssets[i],
					nonPositionBufferAssets[i],
					indexBufferAssets[i],
					material,
					localBounds);
			}
			pbrtMesh->setPosition(vec3(50.0f, -5.0f, 0.0f));
			pbrtMesh->setScale(10.0f);

			scene.addStaticMesh(pbrtMesh);
		}
	}

	scene.sun.direction = SUN_DIRECTION;
	scene.sun.illuminance = SUN_ILLUMINANCE;
}

void TestApplication::destroyResources()
{
	meshSplatting.destroyResources();
	delete ground;
	delete wallA;
	if (LOAD_PBRT_FILE && pbrtMesh != nullptr) delete pbrtMesh;

	scene.skyboxTexture.reset();
}
