#include "app_base.h"

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

AppBase::AppBase()
{
	hWnd = nullptr;
	x = y = 0;
	width = 800; height = 600;
	title = L"title here";

	// According to MSDN this is a fixed value, so don't query it every time.
	QueryPerformanceFrequency(&time_freq);
	elapsedSecondsFromStart = 0.0f;
}

AppBase::~AppBase()
{
	//
}

void AppBase::setPosition(unsigned int x_, unsigned int y_)
{
	x = x_;
	y = y_;
	if (hWnd)
	{
		::MoveWindow(hWnd, x, y, width, height, false);
	}
}

void AppBase::setSize(unsigned int width_, unsigned int height_)
{
	width = width_;
	height = height_;
	if (hWnd)
	{
		::MoveWindow(hWnd, x, y, width, height, false);
	}
}

void AppBase::setTitle(std::wstring title_)
{
	title = title_;
	if (hWnd)
	{
		::SetWindowTextW(hWnd, title.c_str());
	}
}

void AppBase::run(HINSTANCE hInst_, int nCmdShow_, std::wstring windowClass_)
{
	hInst = hInst_;
	nCmdShow = nCmdShow_;
	windowClass = windowClass_;

	MyRegisterClass(hInst);

	if (!InitInstance(hInst, nCmdShow))
	{
		return;
	}

	bool init = onInitialize();
	if (!init)
	{
		MessageBox(0, L"Initialization failed", L"FATAL ERROR", MB_OK);
		return;
	}

	QueryPerformanceCounter(&time_prev);

	MSG msg;
	while (true)
	{
		QueryPerformanceCounter(&time_curr);
		float elapsed = static_cast<float>(time_curr.QuadPart - time_prev.QuadPart) / time_freq.QuadPart;
		elapsedSecondsFromStart = static_cast<float>(time_curr.QuadPart) / time_freq.QuadPart;

		if (max_fps > 0.001f && elapsed > min_elapsed)
		{
			onUpdate(elapsed);
			time_prev = time_curr;
		}

		bool exitLoop = false;

		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				exitLoop = true;
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (exitLoop)
		{
			break;
		}
	}
	
	onTerminate();
}

ATOM AppBase::MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	//wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_STUDYDIRECTX12));
	wcex.hIcon = nullptr;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	//wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_STUDYDIRECTX12);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = windowClass.c_str();
	//wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
	wcex.hIconSm = nullptr;

	return RegisterClassExW(&wcex);
}

BOOL AppBase::InitInstance(HINSTANCE hInstance, int nCmdShow_)
{
	hInst = hInstance;
	nCmdShow = nCmdShow_;

	hWnd = ::CreateWindowW(
		windowClass.c_str(),
		title.c_str(),
		WS_OVERLAPPEDWINDOW,
		x, y,
		width, height,
		nullptr, nullptr, hInstance, nullptr);

	if (!hWnd)
	{
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
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
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
	return 0;
}
