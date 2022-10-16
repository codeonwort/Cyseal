#include "image_loader.h"
#include "util/string_conversion.h"
#include "util/resource_finder.h"

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
