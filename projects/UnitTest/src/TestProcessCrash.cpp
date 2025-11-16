#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "core/engine.h"
#include "core/win/windows_application.h"
#include "world/scene.h"
#include "world/camera.h"

//#define RENDERER_TYPE        ERendererType::Standard
#define RENDERER_TYPE        ERendererType::Null

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

class TestApplication : public WindowsApplication
{
public:
	TestApplication(CysealEngine* engine)
		: cysealEngine(engine)
	{
	}

protected:
	virtual bool onInitialize() override
	{
		SwapChainCreateParams swapChainParams{
			.bHeadless          = false,
			.nativeWindowHandle = getHWND(),
			.windowType         = EWindowType::WINDOWED,
			.windowWidth        = WINDOW_WIDTH,
			.windowHeight       = WINDOW_HEIGHT,
		};
		CysealEngineCreateParams engineInit{
			.renderDevice = RenderDeviceCreateParams{
				.swapChainParams  = swapChainParams,
				.rawAPI           = ERenderDeviceRawAPI::DirectX12,
				.raytracingTier   = ERaytracingTier::MaxTier,
				.bDoubleBuffering = true,
			},
			.rendererType = RENDERER_TYPE,
		};

		cysealEngine->startup(engineInit);

		// May overwritten by world.
		camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
		camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

		exitCounter = 0;

		return true;
	}
	
	virtual void onTick(float deltaSeconds) override
	{
		if (exitCounter++ > 3)
		{
			terminateApplication();
		}
		else
		{
			SceneProxy* sceneProxy = scene.createProxy();
			RendererOptions rendererOptions{};

			// #todo-test: Crashes due to no ImGui operations
			cysealEngine->renderScene(sceneProxy, &camera, rendererOptions);

			delete sceneProxy;
		}
	}

	virtual void onTerminate() override
	{
		cysealEngine->shutdown();
	}

private:
	CysealEngine* cysealEngine = nullptr;

	Scene scene;
	Camera camera;
	uint32 exitCounter = 0;
};


namespace UnitTest
{
	TEST_CLASS(TestProcessCrash)
	{
	public:
		// #todo-fatal: Unit test passes even if the process crashes :(
		TEST_METHOD(EngineStartupNoCrash)
		{
			HWND nativeWindowHandle = NULL;

			CysealEngine cysealEngine;

			WindowsApplication* app = new TestApplication(&cysealEngine);
			app->setWindowTitle(L"Hello world");
			app->setWindowPosition(WINDOW_X, WINDOW_Y);
			app->setWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);

			ApplicationCreateParams createParams;
			createParams.nativeWindowHandle = nativeWindowHandle;
			createParams.applicationName = L"StudyDirectX12";

			// Enters the main loop.
			EApplicationReturnCode ret = app->launch(createParams);
			static_cast<void>(ret);

			Assert::IsTrue(ret == EApplicationReturnCode::Ok);
		}
	};
}
