#pragma once

#include <Windows.h> // #todo-crossplatform: Windows only for now

struct CycleCounterParam
{
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
