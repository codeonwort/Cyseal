#pragma once

#include "int_types.h"
#include <string>

// #todo-application: Needs?
//enum class EApplicationInterface : uint8
//{
//	GUI = 0,
//	CUI = 1,
//};

enum class EApplicationReturnCode : int32
{
	Ok          = 0,
	RandomError = 1
};

struct ApplicationCreateParams
{
	void* nativeWindowHandle = nullptr;
	std::wstring applicationName = L"ApplicationName";
	//int32 numProgramArguments = 0;
	//char** programArguments = nullptr;
};

class ApplicationBase
{
public:
	virtual ~ApplicationBase() {}

	virtual void setWindowPosition(int32 x, int32 y) = 0;
	virtual void setWindowSize(uint32 width, uint32 height) = 0;
	virtual void setWindowTitle(const std::wstring& title) = 0;

	virtual EApplicationReturnCode launch(const ApplicationCreateParams& createParams) = 0;
};
