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
		engineInit.renderDevice.windowWidth = 400;
		engineInit.renderDevice.windowHeight = 300;
		engineInit.renderDevice.raytracingTier = ERaytracingTier::MaxTier;
		engineInit.renderDevice.bDoubleBuffering = true;
		engineInit.rendererType = ERendererType::Standard;

		cysealEngine.startup(engineInit);

		// May overwritten by world.
		camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
		camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

		return true;
	}
	
	virtual void onTick(float deltaSeconds) override {}

	virtual void onTerminate() override {}

private:
	Scene scene;
	Camera camera;
};


namespace UnitTest
{
	TEST_CLASS(TestSTL)
	{
	public:
		TEST_METHOD(EngineStartupNoCrash)
		{
			//
		}
	};
}
