#include "image_loader.h"
#include "core/platform.h"
#include "util/resource_finder.h"

// ------------------------------------------------
// String conversion utils
// 
// 
void str_to_wstr(const std::string& inStr, std::wstring& outStr);
void wstr_to_str(const std::wstring& inStr, std::string& outStr);
#if PLATFORM_WINDOWS
	#include <Windows.h>
#else
	// #todo-crossplatform: std::codecvt is deprecated in C++17.
	#error Not implemented yet
#endif
void str_to_wstr(const std::string& inStr, std::wstring& outStr)
{
	int size = ::MultiByteToWideChar(CP_ACP, 0, inStr.data(), (int)inStr.size(), NULL, 0);
	outStr.assign(size, 0);
	::MultiByteToWideChar(CP_ACP, 0, inStr.data(), (int)inStr.size(), const_cast<wchar_t*>(outStr.data()), size);
}
void wstr_to_str(const std::wstring& inStr, std::string& outStr)
{
	int size = ::WideCharToMultiByte(CP_ACP, 0, inStr.data(), (int)inStr.size(), NULL, 0, NULL, NULL);
	outStr.assign(size, 0);
	::WideCharToMultiByte(CP_ACP, 0, inStr.data(), (int)inStr.size(), const_cast<char*>(outStr.data()), size, NULL, NULL);
}

// ------------------------------------------------
// <stb_image> wrapper
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

bool loadImage_internal(char const* filename, ImageLoadData& outData)
{
	int width, height, numComponents;
	unsigned char* buffer = ::stbi_load(filename, &width, &height, &numComponents, 0);

	outData.buffer = reinterpret_cast<uint8*>(buffer);
	outData.length = static_cast<uint32>(width * height * numComponents);
	outData.width = static_cast<uint32>(width);
	outData.height = static_cast<uint32>(height);
	outData.numComponents = static_cast<uint32>(numComponents);

	return buffer != nullptr;
}

// ------------------------------------------------
// ImageLoader

bool ImageLoader::load(const std::wstring& path, ImageLoadData& outData)
{
	std::wstring wsPath = ResourceFinder::get().find(path);
	std::string sPath;
	wstr_to_str(wsPath, sPath);
	return loadImage_internal(sPath.c_str(), outData);
}
