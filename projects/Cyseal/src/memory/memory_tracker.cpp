#include "memory_tracker.h"
#include "custom_new_delete.h"
#include "core/critical_section.h"
#include "util/logging.h"

#include <cstdio>
#include <atomic>
#include <map>
#include <mutex>

DEFINE_LOG_CATEGORY_STATIC(LogMemory);

// Well... constructors of member variables in MemoryTracker
// causes infinite loop. Let's make all of them global variables...
namespace memtrack
{
	template<typename T>
	class allocatorUntracked: public std::allocator<T>
	{
	public:
		//using value_type = T;
		//using value_type = std::pair<T, std::pair<size_t, EMemoryTag>>;
		allocatorUntracked() noexcept {}

		template<class Other>
		allocatorUntracked(const allocatorUntracked<Other>& _Right) {}

		inline T* allocate(std::size_t n)
		{
			return static_cast<T*>(cyseal_private::customMalloc(n * sizeof(T), EMemoryTag::Untracked));
		}
		inline void deallocate(T* p, std::size_t n) noexcept
		{
			cyseal_private::customFree(p);
		}
	};
	using TrackerKey = void*;
	using TrackerValue = std::pair<size_t, EMemoryTag>;
	using TrackerTable = std::map<TrackerKey, TrackerValue, std::less<TrackerKey>, allocatorUntracked<std::pair<const TrackerKey, TrackerValue>>>;

	static MemoryTracker*      g_instance = nullptr;
	static std::once_flag      onceFlag;

	static bool                bDestroyed = false;
	static std::atomic<size_t> totalAllocated[(int)EMemoryTag::Count];
	static TrackerTable*       trackerTable;
	static CriticalSection*    trackerTableCS;
}

MemoryTracker& MemoryTracker::get()
{
	std::call_once(memtrack::onceFlag, []() {
		memtrack::g_instance = new(EMemoryTag::Untracked) MemoryTracker();
		memtrack::g_instance->initialize();
	});
	return *memtrack::g_instance;
}

MemoryTracker::MemoryTracker()
{
}

MemoryTracker::~MemoryTracker()
{
	terminate();
}

void MemoryTracker::initialize()
{
	memtrack::bDestroyed = false;
	for (int i = 0; i < (int)EMemoryTag::Count; ++i)
	{
		memtrack::totalAllocated[i] = 0;
	}
	memtrack::trackerTable = new(EMemoryTag::Untracked) memtrack::TrackerTable();
	memtrack::trackerTableCS = new(EMemoryTag::Untracked) CriticalSection();
}

void MemoryTracker::terminate()
{
	if (memtrack::bDestroyed)
	{
		return;
	}
	memtrack::bDestroyed = true;

	delete memtrack::trackerTable;
	delete memtrack::trackerTableCS;
}

void MemoryTracker::increase(void* ptr, size_t sz, EMemoryTag tag)
{
	memtrack::totalAllocated[(int)tag] += sz;

	memtrack::trackerTableCS->Enter();
	memtrack::trackerTable->insert(std::pair{ ptr, std::pair{ sz, tag } });
	memtrack::trackerTableCS->Leave();
}

void MemoryTracker::decrease(void* ptr)
{
	if (memtrack::bDestroyed)
	{
		return;
	}

	size_t sz;
	EMemoryTag tag;
	bool found = false;

	memtrack::trackerTableCS->Enter();
	auto it = memtrack::trackerTable->find(ptr);
	if (it != memtrack::trackerTable->end())
	{
		found = true;
		sz = it->second.first;
		tag = it->second.second;
		memtrack::trackerTable->erase(it);
	}
	memtrack::trackerTableCS->Leave();

	if (found)
	{
		memtrack::totalAllocated[(int)tag] -= sz;
	}
}

void MemoryTracker::report()
{
	for (int i = 0; i < (int)EMemoryTag::Count; ++i)
	{
		size_t total = memtrack::totalAllocated[i];
		CYLOG(LogMemory, Log, L"tag = %d, total size = %zu", i, total);
		//std::printf("tag = %d, total size = %zu\n", i, total);
	}
}

size_t MemoryTracker::getTotalBytes(EMemoryTag tag) const
{
	size_t total = memtrack::totalAllocated[(int)tag];
	return total;
}
