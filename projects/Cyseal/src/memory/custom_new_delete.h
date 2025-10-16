#pragma once

#include "memory_tag.h"

void* operator new(std::size_t sz, EMemoryTag memoryTag);

// #note: This variant is not actually used but warning C4291...
void operator delete(void* ptr, EMemoryTag memoryTag) noexcept;
