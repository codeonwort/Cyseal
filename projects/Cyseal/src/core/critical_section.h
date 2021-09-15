#pragma once

// #todo-crossplatform
#include "win/windows_critical_section.h"
#define CriticalSection WindowsCriticalSection

struct ScopedCriticalSection : public CriticalSection
{
	ScopedCriticalSection();
	~ScopedCriticalSection();
};
