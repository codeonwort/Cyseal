#include "critical_section.h"
#include <Windows.h>

ScopedCriticalSection::ScopedCriticalSection()
{
	Enter();
};

ScopedCriticalSection::~ScopedCriticalSection()
{
	Leave();
};
