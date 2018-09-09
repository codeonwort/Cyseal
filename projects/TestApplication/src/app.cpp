#include "app.h"

// engine test
#include "core/engine.h"
#include "memory/mem_alloc.h"
#include "util/unit_test.h"

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
	engineInit.renderDevice.rawAPI = ERenderDeviceRawAPI::DirectX12;
	engineInit.renderDevice.hwnd = getHWND();
	engineInit.renderDevice.windowType = EWindowType::WINDOWED;
	engineInit.renderDevice.windowWidth = getWidth();
	engineInit.renderDevice.windowHeight = getHeight();

	cysealEngine.startup(engineInit);
	
	return true;
}

bool Application::onUpdate(float dt)
{
	wchar_t buf[256];
	swprintf_s(buf, L"Hello World / FPS: %.2f", 1.0f / dt);
	setTitle(std::wstring(buf));

	cysealEngine.getRenderDevice()->draw();

	return true;
}

bool Application::onTerminate()
{
	cysealEngine.shutdown();

	return true;
}
