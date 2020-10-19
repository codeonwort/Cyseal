#pragma once

#include <Windows.h> // #todo-crossplatform: Windows only for now
#include <string>

class AppBase
{

public:
	AppBase();
	virtual ~AppBase();
	void setPosition(unsigned int x, unsigned int y);
	void setSize(unsigned int width, unsigned int height);
	void setTitle(std::wstring title);
	//void setFullscreen(bool isFullscreen);
	void run(HINSTANCE hInst, int nCmdShow, std::wstring windowClass);

protected:
	virtual bool onInitialize() = 0;
	virtual bool onUpdate(float dt) = 0; // dt = elapsed time in seconds
	virtual bool onTerminate() = 0;

	inline unsigned int getWidth() { return width; }
	inline unsigned int getHeight() { return height; }
	inline HWND getHWND() { return hWnd; }

	inline float getElapsedSecondsFromStart() const { return elapsedSecondsFromStart; }
	inline void setFPSLimit(float limit) { min_elapsed = 1.0f / (max_fps = limit); }

private:
	ATOM MyRegisterClass(HINSTANCE hInstance);
	BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);

private:
	HINSTANCE hInst;
	int nCmdShow;
	HWND hWnd;
	std::wstring windowClass;

	// window properties
	std::wstring title;
	unsigned int x, y;
	unsigned int width, height;
	//bool fullscreen; // #todo

	// timer
	LARGE_INTEGER time_curr;
	LARGE_INTEGER time_prev;
	LARGE_INTEGER time_freq;
	float max_fps = 120.0f;
	float min_elapsed = 1.0f / 120.0f;
	float elapsedSecondsFromStart;

};
