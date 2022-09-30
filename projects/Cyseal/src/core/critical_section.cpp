#include "critical_section.h"

ScopedCriticalSection::ScopedCriticalSection()
{
	Enter();
};

ScopedCriticalSection::~ScopedCriticalSection()
{
	Leave();
};
