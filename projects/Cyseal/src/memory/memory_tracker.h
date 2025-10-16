#pragma once

#include "memory_tag.h"

class MemoryTracker
{
public:
	static MemoryTracker& get();

private:
	MemoryTracker();
	~MemoryTracker();

public:
	void initialize();
	void terminate();

	void increase(void* ptr, size_t sz, EMemoryTag tag);
	void decrease(void* ptr);

	void report();
};
