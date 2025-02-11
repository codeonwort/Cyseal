#include "logging.h"
#include "core/platform.h"

#if PLATFORM_WINDOWS
	#include <Windows.h>
#endif

#define LOG_BUFFER_SIZE 10240

void Logger::log(const char* inCategory, LogLevel inLevel, const wchar_t* inMessage...)
{
	wchar_t fmtBuffer[LOG_BUFFER_SIZE];
	va_list argptr;
	va_start(argptr, inMessage);
	// #todo-crossplatform: Is this a standard or MSVC-specific?
	_vswprintf_p(fmtBuffer, LOG_BUFFER_SIZE, inMessage, argptr);
	va_end(argptr);

	wchar_t buffer[LOG_BUFFER_SIZE];
	swprintf_s(buffer, L"[%S][%S]%s\n", inCategory, LogLevelStrings[inLevel], fmtBuffer);

	// Print
	wprintf_s(buffer);

#if PLATFORM_WINDOWS
	::OutputDebugStringW(buffer);
#else
	#error Not implemented yet
#endif
}
