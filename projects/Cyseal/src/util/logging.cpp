#include "logging.h"
#include "core/platform.h"

#if PLATFORM_WINDOWS
	#include <Windows.h>
#endif

void Logger::log(const char* inCategory, LogLevel inLevel, const wchar_t* inMessage...)
{
	wchar_t fmtBuffer[1024];
	va_list argptr;
	va_start(argptr, inMessage);
	// #todo-crossplatform: Is this a standard or MSVC-specific?
	_vswprintf_p(fmtBuffer, 1024, inMessage, argptr);
	va_end(argptr);

	wchar_t buffer[1024];
	swprintf_s(buffer, L"[%S][%S]%s\n", inCategory, LogLevelStrings[inLevel], fmtBuffer);

	// Print
	wprintf_s(buffer);

#if PLATFORM_WINDOWS
	::OutputDebugStringW(buffer);
#else
	#error Not implemented yet
#endif
}
