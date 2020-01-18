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
#define RAYTRACING_TIER  ERayTracingTier::Tier_1_0
#define WINDOW_TYPE      EWindowType::WINDOWED
#define RENDERER_TYPE    ERendererType::Forward


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
	for (auto& v : icosphere.positions)
	{
		v *= 0.5f;
		v.z += 1.0f;
	}

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

	staticMesh = new StaticMesh;
	staticMesh->addSection(vertexBuffer, indexBuffer, nullptr);

	scene.addStaticMesh(staticMesh);
}

void Application::destroyResources()
{
	delete staticMesh;
}
