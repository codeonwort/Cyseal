#include "app.h"
#include "core/core_minimal.h"
#include "render/static_mesh.h"
#include "render/buffer.h"
#include "geometry/primitive.h"

// engine test
#include "util/unit_test.h"
#include "render/render_device.h"
#include "render/render_command.h"

/* -------------------------------------------------------
					CONFIGURATION
--------------------------------------------------------*/
#define RAW_API          ERenderDeviceRawAPI::DirectX12
//#define RAW_API          ERenderDeviceRawAPI::Vulkan
#define RAYTRACING_TIER  ERayTracingTier::Tier_1_0
#define WINDOW_TYPE      EWindowType::WINDOWED
#define RENDERER_TYPE    ERendererType::Forward

#define CAMERA_POSITION  vec3(0.0f, 0.0f, 20.0f)
#define CAMERA_LOOKAT    vec3(0.0f, 0.0f, 0.0f)
#define CAMERA_UP        vec3(0.0f, 1.0f, 0.0f)

#define MESH_COUNT           10
#define MESH_POSITION        vec3(-20.0f, 0.0f, -1.0f)
#define MESH_POSITION_DELTA  vec3(4.0f, 0.0f, 0.0f)
#define MESH_SCALE           1.0f

CysealEngine cysealEngine;

class UnitTestHello : public UnitTest
{
	virtual bool runTest() override
	{
		return true;
	}
};
DEFINE_UNIT_TEST(UnitTestHello);

bool Application::onInitialize()
{
	CysealEngineCreateParams engineInit;
	engineInit.renderDevice.rawAPI          = RAW_API;
	engineInit.renderDevice.rayTracingTier  = RAYTRACING_TIER;
	engineInit.renderDevice.hwnd            = getHWND();
	engineInit.renderDevice.windowType      = WINDOW_TYPE;
	engineInit.renderDevice.windowWidth     = getWidth();
	engineInit.renderDevice.windowHeight    = getHeight();
	engineInit.rendererType                 = RENDERER_TYPE;

	cysealEngine.startup(engineInit);

	createResources();

	camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	
	return true;
}

bool Application::onUpdate(float dt)
{
	wchar_t buf[256];
	swprintf_s(buf, L"Hello World / FPS: %.2f", 1.0f / dt);
	setTitle(std::wstring(buf));

	SceneProxy* sceneProxy = scene.createProxy();

	cysealEngine.getRenderer()->render(sceneProxy, &camera);

	delete sceneProxy;

	return true;
}

bool Application::onTerminate()
{
	destroyResources();

	cysealEngine.shutdown();

	return true;
}

void Application::createResources()
{
	Geometry icosphere;
	GeometryGenerator::icosphere(3, icosphere);

	float* vertexData = reinterpret_cast<float*>(icosphere.positions.data());
	uint32* indexData = icosphere.indices.data();

	// #todo: don't deal with allocator, list, and queue here...
	gRenderDevice->getCommandAllocator()->reset();
	gRenderDevice->getCommandList()->reset();

	VertexBuffer* vertexBuffer = gRenderDevice->createVertexBuffer(vertexData, (uint32)(icosphere.positions.size() * 3 * sizeof(float)), sizeof(float) * 3);
	IndexBuffer* indexBuffer = gRenderDevice->createIndexBuffer(indexData, (uint32)(icosphere.indices.size() * sizeof(uint32)), EPixelFormat::R32_UINT);

	gRenderDevice->getCommandList()->close();
	gRenderDevice->getCommandQueue()->executeCommandList(gRenderDevice->getCommandList());
	gRenderDevice->flushCommandQueue();

	for (uint32 i = 0; i < MESH_COUNT; ++i)
	{
		StaticMesh* staticMesh = new StaticMesh;
		staticMesh->addSection(vertexBuffer, indexBuffer, nullptr);

		staticMesh->getTransform().setPosition(MESH_POSITION + ((float)i * MESH_POSITION_DELTA));
		staticMesh->getTransform().setScale(MESH_SCALE);

		scene.addStaticMesh(staticMesh);
		staticMeshes.push_back(staticMesh);
	}
}

void Application::destroyResources()
{
	for (uint32 i = 0; i < staticMeshes.size(); ++i)
	{
		delete staticMeshes[i];
	}
	staticMeshes.clear();
}
