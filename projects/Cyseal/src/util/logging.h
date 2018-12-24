#pragma once

#include <stdint.h>
#include <stdio.h>
#include <debugapi.h>

enum LogLevel
{
	Log     = 0,
	Warning = 1,
	Error   = 2,
	Fatal   = 3
};

static char* LogLevelStrings[] = { "Log", "Warning", "Error", "Fatal" };

struct LogStructBase
{
	LogStructBase(const char* inCategory, LogLevel inLevel, const TCHAR* inMessage)
	{
		wchar_t buffer[1024];
		swprintf_s(buffer, L"[%S][%S]%s\n", inCategory, LogLevelStrings[inLevel], inMessage);

		// #todo: output to somewhere not stdout (log file, separate GUI, etc...)
		wprintf_s(buffer);
		OutputDebugStringW(buffer);
	}
};

#define DECLARE_LOG_CATEGORY(Category) struct LogStruct_##Category;
#define DEFINE_LOG_CATEGORY(Category)                                             \
	struct LogStruct_##Category : LogStructBase									  \
	{																			  \
		LogStruct_##Category(LogLevel inLevel, const TCHAR* inMessage)	          \
			: LogStructBase(#Category, inLevel, inMessage)						  \
		{}																		  \
	};																			  \

#define CYLOG(Category, Level, Message) { LogStruct_##Category(Level, Message); }
