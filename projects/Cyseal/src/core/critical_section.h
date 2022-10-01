#pragma once

#include "platform.h"

#if PLATFORM_WINDOWS
	#include "win/windows_critical_section.h"
	#define CriticalSection WindowsCriticalSection
#else
	#error Not implemented yet
#endif

struct ScopedCriticalSection : public CriticalSection
{
	ScopedCriticalSection();
	~ScopedCriticalSection();
};
