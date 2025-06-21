#include "logging.h"
#include "core/platform.h"

#if PLATFORM_WINDOWS
	#include <Windows.h>
#endif

#define LOG_BUFFER_SIZE 1024

const LogLevel IGNORE_LOG_LESS_THAN = LogLevel::Log;

void Logger::log(const char* inCategory, LogLevel inLevel, const wchar_t* inMessage...)
{
	if ((int)inLevel < (int)IGNORE_LOG_LESS_THAN)
	{
		return;
	}

	std::vector<wchar_t> fmtBuffer(LOG_BUFFER_SIZE);
	int fmtResult = -1;
	while (fmtResult < 0)
	{
		va_list argptr;
		va_start(argptr, inMessage);
		fmtResult = std::vswprintf(fmtBuffer.data(), fmtBuffer.size(), inMessage, argptr);
		va_end(argptr);

		if (fmtResult < 0)
		{
			fmtBuffer.resize(fmtBuffer.size() * 2);
		}
	}

	std::vector<wchar_t> buffer(fmtBuffer.size());
	std::swprintf(buffer.data(), buffer.size(), L"[%S][%S]%s\n", inCategory, LogLevelStrings[inLevel], fmtBuffer.data());

	// Print
	std::wprintf(buffer.data());

#if PLATFORM_WINDOWS
	::OutputDebugStringW(buffer.data());
#else
	#error Not implemented yet
#endif
}
