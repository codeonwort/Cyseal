// Custom new and delete operators for memory tracking.
// https://en.cppreference.com/w/cpp/memory/new/operator_new

// #note: Trackers require atomic operations which may severely downgrade alloc performance.
//        Disable it for final build.
#define ENABLE_MEMORY_TRACKING 1

// --------------------------------------------------------

#include "memory_tag.h"
#include "core/assertion.h"
#if ENABLE_MEMORY_TRACKING
	#include "memory_tracker.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <new>

// --------------------------------------------------------

namespace cyseal_private
{
	void* customMalloc(std::size_t sz, EMemoryTag memoryTag)
	{
		if (sz == 0)
		{
			throw std::bad_alloc{};
			//++sz; // avoid std::malloc(0) which may return nullptr on success
		}

		if (void* ptr = std::malloc(sz))
		{
#if ENABLE_MEMORY_TRACKING
			if (memoryTag != EMemoryTag::Untracked)
			{
				MemoryTracker::get().increase(ptr, sz, memoryTag);
			}
#endif
			return ptr;
		}

		throw std::bad_alloc{};
	}

	void customFree(void* ptr)
	{
		delete ptr;
	}
}

void* operator new(std::size_t sz)
{
	return cyseal_private::customMalloc(sz, EMemoryTag::Etc);
}

void* operator new(std::size_t sz, EMemoryTag memoryTag)
{
	CHECK(memoryTag != EMemoryTag::Count);
	return cyseal_private::customMalloc(sz, memoryTag);
}

void operator delete(void* ptr) noexcept
{
#if ENABLE_MEMORY_TRACKING
	MemoryTracker::get().decrease(ptr);
#endif
	std::free(ptr);
}

void operator delete(void* ptr, EMemoryTag memoryTag) noexcept
{
	operator delete(ptr);
}
