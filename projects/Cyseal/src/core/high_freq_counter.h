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

	float stopWithMilliseconds()
	{
		auto endTime = std::chrono::system_clock::now();
		auto diff = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
		return (float)diff / 1000.0f;
	}

private:
	std::chrono::time_point<std::chrono::system_clock> startTime;
};
