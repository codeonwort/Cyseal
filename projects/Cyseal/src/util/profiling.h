#pragma once

#if 0
#include <Windows.h> // #todo-crossplatform: Windows only for now

struct CycleCounterParam
{
	// #todo-profiling: initialize() is not being called.
	static void initialize();
	static LARGE_INTEGER frequency;
};

struct CycleCounter
{
	CycleCounter(const char* counterName);
	~CycleCounter();
private:
	const char* name;
	LARGE_INTEGER start;
};

#define SCOPED_CYCLE_COUNTER(CounterName) CycleCounter __cyseal_cycle_counter_##CounterName(#CounterName);
#endif

// Represent PIX events on CPU threads
struct ScopedCPUEvent
{
	ScopedCPUEvent(const char* inEventName);
	~ScopedCPUEvent();
};

#define SCOPED_CPU_EVENT(eventName) ScopedCPUEvent scopedCPUEvent_##eventName(#eventName)
#define SCOPED_CPU_EVENT_STRING(eventString) ScopedCPUEvent scopedCPUEvent_##eventString(eventString)
