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

#include <algorithm>

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
#define CAMERA_Z_NEAR        1.0f
#define CAMERA_Z_FAR         10000.0f

#define MESH_ROWS            3
#define MESH_COLS            6
#define MESH_GROUP_CENTER    vec3(0.0f, 10.0f, 0.0f)
#define MESH_SPACE_X         10.0f
#define MESH_SPACE_Y         8.0f
#define MESH_SPACE_Z         4.0f
#define MESH_SCALE           3.0f

#define CRUMPLED_WORLD       1

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
			vec3 posDelta = vec3(5.0f * sinf(elapsed), 0.0f, 3.0f * cosf(elapsed));
			camera.lookAt(CAMERA_POSITION + posDelta, CAMERA_LOOKAT + posDelta, CAMERA_UP);
			//ground->getTransform().setScale(1.0f + 0.2f * cosf(elapsed));
			ground->setRotation(vec3(0.0f, 1.0f, 0.0f), elapsed * 30.0f);
		}

		// Animate balls to see if update of BLAS instance transforms is going well.
		static float ballTime = 0.0f;
		ballTime += deltaSeconds;
		for (size_t i = 0; i < balls.size(); ++i)
		{
			vec3 p = ballOriginalPos[i];
			if (i % 3 == 0) p.x += 2.0f * Cymath::cos(ballTime);
			else if (i % 3 == 1) p.y += 2.0f * Cymath::cos(ballTime);
			else if (i % 3 == 2) p.z += 2.0f * Cymath::cos(ballTime);
			balls[i]->setPosition(p);

			balls[i]->setScale(MESH_SCALE * (1.0f + 0.3f * Cymath::sin(i + ballTime)));
		}
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

		SceneProxy* sceneProxy = scene.createProxy();

		cysealEngine.getRenderer()->render(sceneProxy, &camera);

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
	constexpr uint32 NUM_GEOM_ASSETS = 7;
	constexpr uint32 NUM_LODs = 2;

	std::vector<std::vector<Geometry*>> geometriesLODs(NUM_GEOM_ASSETS);
	for (uint32 i = 0; i < NUM_GEOM_ASSETS; ++i)
	{
		geometriesLODs[i].resize(NUM_LODs);
		geometriesLODs[i][0] = new Geometry;
		geometriesLODs[i][1] = new Geometry;

		const float phase = Cymath::randFloatRange(0.0f, 6.28f);
		const float spike = Cymath::randFloatRange(0.0f, 0.2f);
#if CRUMPLED_WORLD
		ProceduralGeometry::spikeBall(*geometriesLODs[i][0], 3, phase, spike);
		ProceduralGeometry::spikeBall(*geometriesLODs[i][1], 1, phase, spike);
#else
		ProceduralGeometry::icosphere(*geometriesLODs[i][0], 3);
		ProceduralGeometry::icosphere(*geometriesLODs[i][1], 1);
#endif
	}

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

	std::shared_ptr<VertexBufferAsset> positionBufferLODs[NUM_GEOM_ASSETS][NUM_LODs];
	std::shared_ptr<VertexBufferAsset> nonPositionBufferLODs[NUM_GEOM_ASSETS][NUM_LODs];
	std::shared_ptr<IndexBufferAsset> indexBufferLODs[NUM_GEOM_ASSETS][NUM_LODs];

	for (uint32 geomIx = 0; geomIx < NUM_GEOM_ASSETS; ++geomIx)
	{
		for (uint32 lod = 0; lod < NUM_LODs; ++lod)
		{
			positionBufferLODs[geomIx][lod] = std::make_shared<VertexBufferAsset>();
			nonPositionBufferLODs[geomIx][lod] = std::make_shared<VertexBufferAsset>();
			indexBufferLODs[geomIx][lod] = std::make_shared<IndexBufferAsset>();
			Geometry* G = geometriesLODs[geomIx][lod];

			ENQUEUE_RENDER_COMMAND(UploadSampleMeshBuffers)(
				[positionBufferAsset = positionBufferLODs[geomIx][lod],
				nonPositionBufferAsset = nonPositionBufferLODs[geomIx][lod],
				indexBufferAsset = indexBufferLODs[geomIx][lod],
				G](RenderCommandList& commandList)
				{
					auto positionBuffer = gVertexBufferPool->suballocate(G->getPositionBufferTotalBytes());
					auto nonPositionBuffer = gVertexBufferPool->suballocate(G->getNonPositionBufferTotalBytes());
					auto indexBuffer = gIndexBufferPool->suballocate(G->getIndexBufferTotalBytes(), G->getIndexFormat());

					positionBuffer->updateData(&commandList, G->getPositionBlob(), G->getPositionStride());
					nonPositionBuffer->updateData(&commandList, G->getNonPositionBlob(), G->getNonPositionStride());
					indexBuffer->updateData(&commandList, G->getIndexBlob(), G->getIndexFormat());

					positionBufferAsset->setGPUResource(std::shared_ptr<VertexBuffer>(positionBuffer));
					nonPositionBufferAsset->setGPUResource(std::shared_ptr<VertexBuffer>(nonPositionBuffer));
					indexBufferAsset->setGPUResource(std::shared_ptr<IndexBuffer>(indexBuffer));

					commandList.enqueueDeferredDealloc(G);
				}
			);
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

	const float x0 = MESH_GROUP_CENTER.x - (float)(MESH_COLS * MESH_SPACE_X) / 2.0f;
	const float y0 = MESH_GROUP_CENTER.y - (float)(MESH_ROWS * MESH_SPACE_Y) / 2.0f;
	uint32 currentGeomIx = 0;
	for (uint32 row = 0; row < MESH_ROWS; ++row)
	{
		for (uint32 col = 0; col < MESH_COLS; ++col)
		{
			StaticMesh* staticMesh = new StaticMesh;

			for (uint32 lod = 0; lod < NUM_LODs; ++lod)
			{
				auto material = std::make_shared<Material>();
				material->albedoTexture = albedoTexture;
				material->albedoMultiplier[0] = (std::max)(0.001f, (float)(col + 0) / MESH_COLS);
				material->albedoMultiplier[1] = (std::max)(0.001f, (float)(row + 0) / MESH_ROWS);
				material->albedoMultiplier[2] = 0.0f;
				//material->roughness = Cymath::randFloat() < 0.5f ? 0.0f : 1.0f;
				material->roughness = 0.0f;

				staticMesh->addSection(
					lod,
					positionBufferLODs[currentGeomIx][lod],
					nonPositionBufferLODs[currentGeomIx][lod],
					indexBufferLODs[currentGeomIx][lod],
					material);
			}

			vec3 pos = MESH_GROUP_CENTER;
			pos.x = x0 + col * MESH_SPACE_X;
			pos.y = y0 + row * MESH_SPACE_Y;
			pos.z -= row * MESH_SPACE_Z;
			pos.z += 2.0f * col * MESH_SPACE_Z / MESH_COLS;
			pos.x += (row & 1) * 0.5f * MESH_SPACE_X;

			staticMesh->setPosition(pos);
			staticMesh->setScale(MESH_SCALE);

			scene.addStaticMesh(staticMesh);
			balls.push_back(staticMesh);
			ballOriginalPos.push_back(pos);

			currentGeomIx = (currentGeomIx + 1) % NUM_GEOM_ASSETS;
		}
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
		ground->addSection(0, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset, material);
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
		material->albedoTexture = gTextureManager->getSystemTextureGreen2D();
		material->roughness = 0.0f;

		wallA = new StaticMesh;
		wallA->addSection(0, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset, material);
		wallA->setPosition(vec3(x0 - MESH_SPACE_X, 0.0f, 0.0f));
		wallA->setRotation(vec3(0.0f, 0.0f, 1.0f), -10.0f);

		scene.addStaticMesh(wallA);
	}

	scene.sun.direction = SUN_DIRECTION;
	scene.sun.illuminance = SUN_ILLUMINANCE;
}

void TestApplication::destroyResources()
{
	for (uint32 i = 0; i < balls.size(); ++i)
	{
		delete balls[i];
	}
	balls.clear();

	delete ground;
	delete wallA;
}
