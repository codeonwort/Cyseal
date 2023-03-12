#include "windows_application.h"
#include "core/assertion.h"
#include "util/profiling.h"

#include "imgui_impl_win32.h"

LRESULT CALLBACK Win32WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
ATOM Win32RegisterClass(HINSTANCE hInstance, const wchar_t* windowClassName);
HWND Win32InitInstance(
	HINSTANCE hInstance, ATOM winClass,
	int x, int y, int width, int height,
	const wchar_t* title);

std::map<HWND, WindowsApplication*> WindowsApplication::hwndToApp;

WindowsApplication::WindowsApplication()
{
	x = y = 0; width = 1024; height = 768;
	title = L"Title here";
}

void WindowsApplication::setWindowPosition(int32 inX, int32 inY)
{
	x = inX;
	y = inY;
	if (hWnd)
	{
		::MoveWindow(hWnd, (int)x, (int)y, (int)width, (int)height, FALSE);
	}
}

void WindowsApplication::setWindowSize(uint32 inWidth, uint32 inHeight)
{
	width = inWidth;
	height = inHeight;
	if (hWnd)
	{
		::MoveWindow(hWnd, (int)x, (int)y, (int)width, (int)height, FALSE);
	}
}

void WindowsApplication::setWindowTitle(const std::wstring& inTitle)
{
	title = inTitle;
	if (hWnd)
	{
		::SetWindowTextW(hWnd, title.c_str());
	}
}

EApplicationReturnCode WindowsApplication::launch(const ApplicationCreateParams& createParams)
{
	HINSTANCE hInstance = (HINSTANCE)createParams.nativeWindowHandle;
	const wchar_t* appName = createParams.applicationName.c_str();

	// According to MSDN this is a fixed value, so don't query it every time.
	::QueryPerformanceFrequency(&time_freq);

	// #todo: Enable dpi awareness for imgui if needed.
	//ImGui_ImplWin32_EnableDpiAwareness();

	winClass = Win32RegisterClass(hInstance, appName);

	hWnd = Win32InitInstance(hInstance, winClass,
		(int)x, (int)y, (int)width, (int)height, title.c_str());
	if (hWnd == NULL)
	{
		return EApplicationReturnCode::RandomError;
	}

	// Update actual viewport size
	RECT clientRect;
	::GetClientRect(hWnd, &clientRect);
	width = clientRect.right - clientRect.left;
	height = clientRect.bottom - clientRect.top;

	WindowsApplication::hwndToApp.insert(std::make_pair(hWnd, this));

	bool bInit = onInitialize();
	if (!bInit)
	{
		::MessageBox(0, L"Initialization failed", L"FATAL ERROR", MB_OK);
		return EApplicationReturnCode::RandomError;
	}

	::QueryPerformanceCounter(&time_prev);

	MSG msg;
	static uint32 frameNumber = 0;
	while (true)
	{
		::QueryPerformanceCounter(&time_curr);
		float deltaSeconds = static_cast<float>(time_curr.QuadPart - time_prev.QuadPart) / time_freq.QuadPart;
		elapsedSecondsFromStart = static_cast<float>(time_curr.QuadPart) / time_freq.QuadPart;

		if (max_fps > 0.001f && deltaSeconds > min_elapsed)
		{
			char eventName[64];
			sprintf_s(eventName, "Frame %u", frameNumber++);
			SCOPED_CPU_EVENT_STRING(eventName);

			onTick(deltaSeconds);
			time_prev = time_curr;
		}

		bool bShouldQuit = false;

		while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				bShouldQuit = true;
				break;
			}

			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}

		if (bShouldQuit)
		{
			break;
		}
	}

	onTerminate();

	return EApplicationReturnCode::Ok;
}

//////////////////////////////////////////////////////////////////////////
// Windows backend

ATOM Win32RegisterClass(HINSTANCE hInstance, const wchar_t* windowClassName)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = Win32WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	//wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_STUDYDIRECTX12));
	wcex.hIcon = nullptr;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	//wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_STUDYDIRECTX12);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = windowClassName;
	//wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
	wcex.hIconSm = nullptr;

	// Uniquely identifies the class being registered.
	ATOM winClass = ::RegisterClassExW(&wcex);
	CHECK(winClass != 0);
	return winClass;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK Win32WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return true;
	}

	switch (message)
	{
		case WM_COMMAND:
		{
			return DefWindowProcW(hWnd, message, wParam, lParam);
		}
		break;
		case WM_KEYDOWN:
		{
			if (wParam == VK_ESCAPE)
			{
				PostQuitMessage(0);
			}
		}
		break;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			static_cast<void>(hdc);
			EndPaint(hWnd, &ps);
		}
		break;
		case WM_SIZE:
		{
			auto it = WindowsApplication::hwndToApp.find(hWnd);
			if (it != WindowsApplication::hwndToApp.end())
			{
				WindowsApplication* winApp = it->second;
				UINT windowWidth = LOWORD(lParam);
				UINT windowHeight = HIWORD(lParam);
				if (windowWidth > 0 && windowHeight > 0)
				{
					winApp->internal_updateWindowSize(windowWidth, windowHeight);
					winApp->onWindowResize(windowWidth, windowHeight);
				}
			}
		}
		break;
		case WM_CLOSE:
		{
			PostQuitMessage(0);
			break;
		}
		default:
		{
			return DefWindowProcW(hWnd, message, wParam, lParam);
		}
	}
	return 0;
}

HWND Win32InitInstance(
	HINSTANCE hInstance, ATOM winClass,
	int x, int y, int width, int height,
	const wchar_t* title)
{
	HWND hWnd = ::CreateWindowW(
		MAKEINTATOM(winClass),
		title,
		WS_OVERLAPPEDWINDOW,
		x, y,
		width, height,
		nullptr, nullptr, hInstance, nullptr);

	if (hWnd == NULL)
	{
		return NULL;
	}

	// #todo: Hmm... sometimes the window is minimized at startup. Does this always solve it?
	::BringWindowToTop(hWnd);

	::ShowWindow(hWnd, SW_SHOW);
	::UpdateWindow(hWnd);

	return hWnd;
}
