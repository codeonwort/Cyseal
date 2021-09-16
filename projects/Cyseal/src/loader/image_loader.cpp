#include "image_loader.h"
#include "util/resource_finder.h"

// string conversion -------------------------------
// https://stackoverflow.com/a/18374698
#include <locale>
#include <codecvt>
void str_to_wstr(const std::string& inStr, std::wstring& outWstr)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;
	outWstr = converterX.from_bytes(inStr);
}
void wstr_to_str(const std::wstring& inWstr, std::string& outStr)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;
	outStr = converterX.to_bytes(inWstr);
}
// -------------------------------------------------

// stb_image wrapper -------------------------------
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

bool ImageLoader::load(const std::wstring& path, ImageLoadData& outData)
{
	std::wstring wsPath = ResourceFinder::get().find(path);
	std::string sPath;
	wstr_to_str(wsPath, sPath);
	return loadImage_internal(sPath.c_str(), outData);
}
