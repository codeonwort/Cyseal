#pragma once

#define NOMINMAX
#include <Windows.h>

struct WindowsCriticalSection
{
	WindowsCriticalSection();
	~WindowsCriticalSection();

	void Enter();
	bool TryEnter();
	void Leave();

	CRITICAL_SECTION nativeCS;
};
