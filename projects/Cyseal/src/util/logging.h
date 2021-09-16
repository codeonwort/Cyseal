#pragma once

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

struct Logger
{
	// #todo-log: Output to somewhere not stdout (log file, separate GUI, etc...)
	// #todo-log: Separate logging thread?
	static void log(const char* inCategory, LogLevel inLevel, const TCHAR* inMessage...)
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
		OutputDebugStringW(buffer);
	}
};

//struct LogStructBase
//{
//	LogStructBase(const char* inCategory, LogLevel inLevel, const TCHAR* inMessage)
//	{
//		wchar_t buffer[1024];
//		swprintf_s(buffer, L"[%S][%S]%s\n", inCategory, LogLevelStrings[inLevel], inMessage);
//
//		// #todo-log: Output to somewhere not stdout (log file, separate GUI, etc...)
//		// #todo-log: Create a separate logging thread
//		wprintf_s(buffer);
//		OutputDebugStringW(buffer);
//	}
//};

struct LogStructBase
{
	LogStructBase(const char* inCategory)
		: category(inCategory)
	{
	}
	const char* category;
};

// Must use with DEFINE_LOG_CATEGORY().
// Any source files that include the header can access this category.
#define DECLARE_LOG_CATEGORY(Category)                                            \
	extern struct LogStruct_##Category : LogStructBase						      \
	{																			  \
		LogStruct_##Category() : LogStructBase(#Category) {}					  \
	} Category;

// Must use with DECLARE_LOG_CATEGORY().
#define DEFINE_LOG_CATEGORY(Category) LogStruct_##Category Category;

// Define in a .cpp file and only can access within that file.
#define DEFINE_LOG_CATEGORY_STATIC(Category)                                      \
	static struct LogStruct_##Category : LogStructBase						      \
	{																			  \
		LogStruct_##Category() : LogStructBase(#Category) {}					  \
	} Category;

#define CYLOG(Category, Level, Message, ...) { Logger::log(Category.category, Level, Message, __VA_ARGS__); }
