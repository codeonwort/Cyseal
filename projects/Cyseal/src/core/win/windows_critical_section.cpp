#include "windows_critical_section.h"

WindowsCriticalSection::WindowsCriticalSection()
{
	::InitializeCriticalSection(&nativeCS);
}

WindowsCriticalSection::~WindowsCriticalSection()
{
	::DeleteCriticalSection(&nativeCS);
}

void WindowsCriticalSection::Enter()
{
	::EnterCriticalSection(&nativeCS);
}

bool WindowsCriticalSection::TryEnter()
{
	return ::TryEnterCriticalSection(&nativeCS);
}

void WindowsCriticalSection::Leave()
{
	::LeaveCriticalSection(&nativeCS);
}
