#pragma once

#include "core/application.h"

#include <map>
#include <Windows.h>

class WindowsApplication : public ApplicationBase
{
public:
	static std::map<HWND, WindowsApplication*> hwndToApp;

public:
	WindowsApplication();

	virtual void setWindowPosition(int32 inX, int32 inY) override;
	virtual void setWindowSize(uint32 inWidth, uint32 inHeight) override;
	virtual void setWindowTitle(const std::wstring& inTitle) override;

	virtual void onWindowResize(uint32 newWidth, uint32 newHeight) {}

	virtual EApplicationReturnCode launch(const ApplicationCreateParams& createParams) override;

	inline float getElapsedSecondsFromStart() const { return elapsedSecondsFromStart; }
	inline void setFPSLimit(float limit) { min_elapsed = 1.0f / (max_fps = limit); }

	void internal_updateWindowSize(uint32 inWidth, uint32 inHeight) { width = inWidth; height = inHeight; }

protected:
	virtual bool onInitialize() = 0;
	virtual void onTick(float deltaSeconds) = 0;
	virtual void onTerminate() = 0;

	inline HWND getHWND() const { return hWnd; }
	inline uint32 getWindowWidth() const { return width; }
	inline uint32 getWindowHeight() const { return height; }
	inline float getAspectRatio() const { return (float)width / height; }

private:
	HWND hWnd = NULL;
	ATOM winClass = NULL;

	// GUI properties
	std::wstring title;
	int32 x, y;
	uint32 width, height;
	//bool bFullscreen; // #todo

	// Timers
	LARGE_INTEGER time_curr;
	LARGE_INTEGER time_prev;
	LARGE_INTEGER time_freq;
	float max_fps = 120.0f;
	float min_elapsed = 1.0f / 120.0f;
	float elapsedSecondsFromStart = 0.0f;
};
