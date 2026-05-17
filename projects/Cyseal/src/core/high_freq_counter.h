#pragma once

#include "int_types.h"
#include <chrono>

class HighFrequencyCounter
{
public:
	void start()
	{
		startTime = std::chrono::system_clock::now();
	}

	uint64 stopWithMilliseconds()
	{
		auto endTime = std::chrono::system_clock::now();
		auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
		return diff;
	}

private:
	std::chrono::time_point<std::chrono::system_clock> startTime;
};
