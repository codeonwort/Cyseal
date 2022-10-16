#include "string_conversion.h"
#include "core/platform.h"

#if PLATFORM_WINDOWS
	#include <Windows.h>
#else
	// #todo-crossplatform: std::codecvt is deprecated in C++17.
	#error Not implemented yet
#endif

void str_to_wstr(const std::string& inStr, std::wstring& outStr)
{
#if PLATFORM_WINDOWS
	int size = ::MultiByteToWideChar(CP_ACP, 0, inStr.data(), (int)inStr.size(), NULL, 0);
	outStr.assign(size, 0);
	::MultiByteToWideChar(CP_ACP, 0, inStr.data(), (int)inStr.size(), const_cast<wchar_t*>(outStr.data()), size);
#else
	#error Not implemented yet
#endif
}
void wstr_to_str(const std::wstring& inStr, std::string& outStr)
{
#if PLATFORM_WINDOWS
	int size = ::WideCharToMultiByte(CP_ACP, 0, inStr.data(), (int)inStr.size(), NULL, 0, NULL, NULL);
	outStr.assign(size, 0);
	::WideCharToMultiByte(CP_ACP, 0, inStr.data(), (int)inStr.size(), const_cast<char*>(outStr.data()), size, NULL, NULL);
#else
	#error Not implemented yet
#endif
}
