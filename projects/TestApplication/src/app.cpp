#include "app.h"

// engine test
#include "core/engine.h"
#include "memory/mem_alloc.h"
#include "util/unit_test.h"

/* -------------------------------------
			CONFIGURATION
--------------------------------------*/
#define RAW_API			ERenderDeviceRawAPI::DirectX12
#define WINDOW_TYPE		EWindowType::WINDOWED
#define RENDERER_TYPE	ERendererType::Forward


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
	engineInit.renderDevice.rawAPI = RAW_API;
	engineInit.renderDevice.hwnd = getHWND();
	engineInit.renderDevice.windowType = WINDOW_TYPE;
	engineInit.renderDevice.windowWidth = getWidth();
	engineInit.renderDevice.windowHeight = getHeight();
	engineInit.rendererType = RENDERER_TYPE;

	cysealEngine.startup(engineInit);
	
	return true;
}

bool Application::onUpdate(float dt)
{
	wchar_t buf[256];
	swprintf_s(buf, L"Hello World / FPS: %.2f", 1.0f / dt);
	setTitle(std::wstring(buf));

	cysealEngine.getRenderer()->render(&sceneProxy, &camera);

	return true;
}

bool Application::onTerminate()
{
	cysealEngine.shutdown();

	return true;
}
