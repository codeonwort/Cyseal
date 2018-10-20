#include "App.h"

AppBase* app = nullptr;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	app = new Application;
	app->setTitle(L"Hello world");
	app->setPosition(200, 200);
	app->setSize(1600, 900);
	app->run(hInstance, nCmdShow, L"StudyDirectX12");

	return 0;
}
