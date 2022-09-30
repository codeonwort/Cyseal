#include "logging.h"
#include <Windows.h> // #todo-crossplatform

void Logger::log(const char* inCategory, LogLevel inLevel, const wchar_t* inMessage...)
{
	wchar_t fmtBuffer[1024];
	va_list argptr;
	va_start(argptr, inMessage);
	_vswprintf_p(fmtBuffer, 1024, inMessage, argptr);
	va_end(argptr);

	wchar_t buffer[1024];
	swprintf_s(buffer, L"[%S][%S]%s\n", inCategory, LogLevelStrings[inLevel], fmtBuffer);

	// Print
	wprintf_s(buffer);
	::OutputDebugStringW(buffer);
}
