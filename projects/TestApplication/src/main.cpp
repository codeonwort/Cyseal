#include "app.h"
#include "core/assertion.h"

#define WINDOW_WIDTH  1600
#define WINDOW_HEIGHT 900

WindowsApplication* app = nullptr;

int APIENTRY wWinMain(
	_In_     HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_     LPWSTR    lpCmdLine,
	_In_     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	app = new TestApplication;
	app->setWindowTitle(L"Hello world");
	app->setWindowPosition(200, 200);
	app->setWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);

	ApplicationCreateParams createParams;
	createParams.nativeWindowHandle = hInstance;
	createParams.applicationName = L"StudyDirectX12";
	
	// Enters the main loop.
	EApplicationReturnCode ret = app->launch(createParams);

	CHECK(ret == EApplicationReturnCode::Ok);

	return 0;
}
