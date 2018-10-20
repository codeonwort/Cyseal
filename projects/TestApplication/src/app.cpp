#include "app.h"
#include "core/core_minimal.h"
#include "render/static_mesh.h"
#include "render/buffer.h"

// engine test
#include "util/unit_test.h"
#include "render/render_device.h"
#include "render/render_command.h"

/* -------------------------------------------------------
					CONFIGURATION
--------------------------------------------------------*/
#define RAW_API          ERenderDeviceRawAPI::DirectX12
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
	float vertexData[] = {
		0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		1.0f, 0.0f, 0.0f };
	int32 indexData[] = { 0, 1, 2 };

	// #todo: don't deal with allocator, list, and queue here...
	gRenderDevice->getCommandAllocator()->reset();
	gRenderDevice->getCommandList()->reset();

	VertexBuffer* vertexBuffer = gRenderDevice->createVertexBuffer(vertexData, sizeof(vertexData), sizeof(float) * 3);
	IndexBuffer* indexBuffer = gRenderDevice->createIndexBuffer(indexData, sizeof(indexData), EPixelFormat::R32_UINT);

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
