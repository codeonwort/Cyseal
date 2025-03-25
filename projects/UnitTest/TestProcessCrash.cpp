#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "core/engine.h"
#include "core/win/windows_application.h"
#include "world/scene.h"
#include "world/camera.h"

#define CAMERA_POSITION      vec3(50.0f, 0.0f, 30.0f)
#define CAMERA_LOOKAT        vec3(50.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

#define WINDOW_X             200
#define WINDOW_Y             200
#define WINDOW_WIDTH         400
#define WINDOW_HEIGHT        300

static CysealEngine cysealEngine;

class TestApplication : public WindowsApplication
{
protected:
	virtual bool onInitialize() override
	{
		CysealEngineCreateParams engineInit;
		engineInit.renderDevice.rawAPI = ERenderDeviceRawAPI::DirectX12;
		engineInit.renderDevice.nativeWindowHandle = getHWND();
		engineInit.renderDevice.windowType = EWindowType::WINDOWED;
		engineInit.renderDevice.windowWidth = WINDOW_WIDTH;
		engineInit.renderDevice.windowHeight = WINDOW_HEIGHT;
		engineInit.renderDevice.raytracingTier = ERaytracingTier::MaxTier;
		engineInit.renderDevice.bDoubleBuffering = true;
		engineInit.rendererType = ERendererType::Standard;

		cysealEngine.startup(engineInit);

		// May overwritten by world.
		camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
		camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

		exitCounter = 0;

		return true;
	}
	
	virtual void onTick(float deltaSeconds) override
	{
		if (exitCounter++ > 120)
		{
			terminateApplication();
		}
	}

	virtual void onTerminate() override
	{
		cysealEngine.shutdown();
	}

private:
	Scene scene;
	Camera camera;
	uint32 exitCounter = 0;
};


namespace UnitTest
{
	TEST_CLASS(TestProcessCrash)
	{
	public:
		TEST_METHOD(EngineStartupNoCrash)
		{
			HWND nativeWindowHandle = NULL;

			WindowsApplication* app = new TestApplication;
			app->setWindowTitle(L"Hello world");
			app->setWindowPosition(WINDOW_X, WINDOW_Y);
			app->setWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);

			ApplicationCreateParams createParams;
			createParams.nativeWindowHandle = nativeWindowHandle;
			createParams.applicationName = L"StudyDirectX12";

			// Enters the main loop.
			EApplicationReturnCode ret = app->launch(createParams);

			CHECK(ret == EApplicationReturnCode::Ok);
		}
	};
}
