#include "app.h"
#include "core/core_minimal.h"
#include "render/material.h"
#include "render/static_mesh.h"
#include "render/gpu_resource.h"
#include "geometry/primitive.h"
#include "loader/image_loader.h"

#include <algorithm>

/* -------------------------------------------------------
					CONFIGURATION
--------------------------------------------------------*/
// 0: DX12 + Standard renderer
// 1: Vulkan + Null renderer
#define RENDERER_PRESET 0

#if RENDERER_PRESET == 0
	#define RAW_API          ERenderDeviceRawAPI::DirectX12
	#define RENDERER_TYPE    ERendererType::Standard
#elif RENDERER_PRESET == 1
	#define RAW_API          ERenderDeviceRawAPI::Vulkan
	#define RENDERER_TYPE    ERendererType::Null
#endif
#define RAYTRACING_TIER      ERayTracingTier::Tier_1_0
#define WINDOW_TYPE          EWindowType::WINDOWED

// #todo: Did I implement left-handedness?
//        It's been too long I worked on this project...
#define CAMERA_POSITION  vec3(0.0f, 0.0f, -20.0f) // Outward from monitor?
#define CAMERA_LOOKAT    vec3(0.0f, 0.0f, 0.0f)
#define CAMERA_UP        vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y     70.0f
#define CAMERA_Z_NEAR    1.0f
#define CAMERA_Z_FAR     10000.0f

#define MESH_ROWS            10
#define MESH_COLS            10
#define MESH_GROUP_CENTER    vec3(0.0f, 0.0f, -1.0f)
#define MESH_SPACE_X         2.0f
#define MESH_SPACE_Y         2.0f
#define MESH_SCALE           1.0f

/* -------------------------------------------------------
					APPLICATION
--------------------------------------------------------*/
CysealEngine cysealEngine;

bool TestApplication::onInitialize()
{
	CysealEngineCreateParams engineInit;
	engineInit.renderDevice.rawAPI             = RAW_API;
	engineInit.renderDevice.rayTracingTier     = RAYTRACING_TIER;
	engineInit.renderDevice.nativeWindowHandle = getHWND();
	engineInit.renderDevice.windowType         = WINDOW_TYPE;
	engineInit.renderDevice.windowWidth        = getWindowWidth();
	engineInit.renderDevice.windowHeight       = getWindowHeight();
	engineInit.rendererType                    = RENDERER_TYPE;

	cysealEngine.startup(engineInit);

	createResources();

	camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	// #todo: Respond to window resize
	camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);
	
	return true;
}

void TestApplication::onTick(float deltaSeconds)
{
	wchar_t buf[256];
	swprintf_s(buf, L"Hello World / FPS: %.2f", 1.0f / deltaSeconds);
	setWindowTitle(std::wstring(buf));

	// #todo-app: Control camera by user input
	{
		//static float elapsed = 0.0f;
		//elapsed += deltaSeconds;
		//vec3 posDelta = vec3(10.0f * sinf(elapsed), 0.0f, 5.0f * cosf(elapsed));
		//camera.lookAt(CAMERA_POSITION + posDelta, CAMERA_LOOKAT + posDelta, CAMERA_UP);
	}

	// #todo: Move rendering loop to engine
	{
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

void TestApplication::createResources()
{
	constexpr uint32 NUM_LODs = 2;

	Geometry icospheres[NUM_LODs];
	GeometryGenerator::icosphere(3, icospheres[0]);
	GeometryGenerator::icosphere(1, icospheres[1]);

	// #todo: Unload image memory when GPU upload is done.
	ImageLoader loader;
	ImageLoadData loadData;
	if (loader.load(L"bee.png", loadData) == false)
	{
		// Fill random image
		loadData.numComponents = 4;
		loadData.width = 256;
		loadData.height = 256;
		loadData.length = 256 * 256 * 4;
		loadData.buffer = new uint8[loadData.length];
		int32 p = 0;
		for (int32 y = 0; y < 256; ++y)
		{
			for (int32 x = 0; x < 256; ++x)
			{
				loadData.buffer[p] = x ^ y;
				loadData.buffer[p+1] = x ^ y;
				loadData.buffer[p+2] = x ^ y;
				loadData.buffer[p+3] = 0xff;
				p += 4;
			}
		}
	}

	float* vertexDataLODs[NUM_LODs];
	uint32* indexDataLODs[NUM_LODs];
	for (uint32 i = 0; i < NUM_LODs; ++i)
	{
		vertexDataLODs[i] = reinterpret_cast<float*>(icospheres[i].positions.data());
		indexDataLODs[i] = icospheres[i].indices.data();
	}

	VertexBuffer* vertexBufferLODs[NUM_LODs];
	IndexBuffer* indexBufferLODs[NUM_LODs];

	ENQUEUE_RENDER_COMMAND(UploadIcosphereBuffers)(
		[NUM_LODs, &icospheres,
		&vertexDataLODs, &indexDataLODs,
		&vertexBufferLODs, &indexBufferLODs](RenderCommandList& commandList)
		{
			for (uint32 i = 0; i < NUM_LODs; ++i)
			{
				uint32 vertexBufferBytes = (uint32)(icospheres[i].positions.size() * 3 * sizeof(float));
				uint32 indexBufferBytes = (uint32)(icospheres[i].indices.size() * sizeof(uint32));
				vertexBufferLODs[i] = gRenderDevice->createVertexBuffer(vertexDataLODs[i], vertexBufferBytes, sizeof(float) * 3);
				indexBufferLODs[i] = gRenderDevice->createIndexBuffer(indexDataLODs[i], indexBufferBytes, EPixelFormat::R32_UINT);
			}
		}
	);
	Texture*& texturePtr = texture;
	ENQUEUE_RENDER_COMMAND(CreateTexture)(
		[&texturePtr, &loadData](RenderCommandList& commandList)
		{
			TextureCreateParams params = TextureCreateParams::texture2D(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
				loadData.width, loadData.height, 1);
			texturePtr = gRenderDevice->createTexture(params);
			texturePtr->uploadData(commandList, loadData.buffer, loadData.getRowPitch(), loadData.getSlicePitch());
			texturePtr->setDebugName(TEXT("Texture_test"));
		}
	);
	FLUSH_RENDER_COMMANDS();

	const float x0 = MESH_GROUP_CENTER.x - (float)(MESH_COLS * MESH_SPACE_X) / 2.0f;
	const float y0 = MESH_GROUP_CENTER.y - (float)(MESH_ROWS * MESH_SPACE_Y) / 2.0f;
	for (uint32 row = 0; row < MESH_ROWS; ++row)
	{
		for (uint32 col = 0; col < MESH_COLS; ++col)
		{
			StaticMesh* staticMesh = new StaticMesh;

			for (uint32 lod = 0; lod < NUM_LODs; ++lod)
			{
				Material* material = new Material;
				material->albedoTexture = texture;
				material->albedoMultiplier[0] = (std::max)(0.001f, (float)(col + 0) / MESH_COLS);
				material->albedoMultiplier[1] = (std::max)(0.001f, (float)(row + 0) / MESH_ROWS);
				material->albedoMultiplier[2] = 0.0f;
				staticMesh->addSection(lod, vertexBufferLODs[lod], indexBufferLODs[lod], material);
			}

			vec3 pos = MESH_GROUP_CENTER;
			pos.x = x0 + col * MESH_SPACE_X;
			pos.y = y0 + row * MESH_SPACE_Y;

			staticMesh->getTransform().setPosition(pos);
			staticMesh->getTransform().setScale(MESH_SCALE);

			scene.addStaticMesh(staticMesh);
			staticMeshes.push_back(staticMesh);
		}
	}
}

void TestApplication::destroyResources()
{
	for (uint32 i = 0; i < staticMeshes.size(); ++i)
	{
		delete staticMeshes[i];
	}
	staticMeshes.clear();
}
