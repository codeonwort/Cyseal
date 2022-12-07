#include "profiling.h"
#include "core/platform.h"
#include <string.h>

#if PLATFORM_WINDOWS
	#include <Windows.h>
	#ifndef _DEBUG
		#define USE_PIX
	#endif
	#include <pix3.h>
#endif

#if 0
void CycleCounterParam::initialize()
{
	QueryPerformanceFrequency(&frequency);
}

CycleCounter::CycleCounter(const char* counterName)
{
	name = counterName;
	QueryPerformanceCounter(&start);
}

CycleCounter::~CycleCounter()
{
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);

	const float seconds = static_cast<float>(end.QuadPart - start.QuadPart) / static_cast<float>(CycleCounterParam::frequency.QuadPart);
	// #todo: log elapsed seconds
}
#endif

ScopedCPUEvent::ScopedCPUEvent(const char* inEventName)
{
#if PLATFORM_WINDOWS
	::PIXBeginEvent(0x00000000, inEventName);
#endif
}
ScopedCPUEvent::~ScopedCPUEvent()
{
#if PLATFORM_WINDOWS
	::PIXEndEvent();
#endif
}
