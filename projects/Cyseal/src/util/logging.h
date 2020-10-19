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

struct Logger
{
	static void log(const char* inCategory, LogLevel inLevel, const TCHAR* inMessage)
	{
		wchar_t buffer[1024];
		swprintf_s(buffer, L"[%S][%S]%s\n", inCategory, LogLevelStrings[inLevel], inMessage);

		// #todo: Output to somewhere not stdout (log file, separate GUI, etc...)
		// #todo: Create a separate logging thread
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
//		// #todo: Output to somewhere not stdout (log file, separate GUI, etc...)
//		// #todo: Create a separate logging thread
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

#define DECLARE_LOG_CATEGORY(Category)                                            \
	extern struct LogStruct_##Category : LogStructBase						      \
	{																			  \
		LogStruct_##Category() : LogStructBase(#Category) {}					  \
	} Category;

#define DEFINE_LOG_CATEGORY(Category) LogStruct_##Category Category;

#define DEFINE_LOG_CATEGORY_STATIC(Category)                                      \
	static struct LogStruct_##Category : LogStructBase						      \
	{																			  \
		LogStruct_##Category() : LogStructBase(#Category) {}					  \
	} Category;

#define CYLOG(Category, Level, Message) { Logger::log(Category.category, Level, Message); }
