#include "profiling.h"
#include <string.h>

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
	// TODO: log elapsed seconds
}
