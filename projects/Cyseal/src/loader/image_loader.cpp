#include "image_loader.h"
#include "util/string_conversion.h"
#include "util/resource_finder.h"

// ------------------------------------------------
// <stb_image> wrapper
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

ImageLoadData* loadImage_internal(char const* filename)
{
	int width, height, numComponents;
	unsigned char* buffer = ::stbi_load(filename, &width, &height, &numComponents, 0);

	if (buffer == nullptr)
	{
		return nullptr;
	}

	ImageLoadData* imageBlob = new ImageLoadData;

	imageBlob->buffer = reinterpret_cast<uint8*>(buffer);
	imageBlob->length = static_cast<uint32>(width * height * numComponents);
	imageBlob->width = static_cast<uint32>(width);
	imageBlob->height = static_cast<uint32>(height);
	imageBlob->numComponents = static_cast<uint32>(numComponents);

	return imageBlob;
}

// ------------------------------------------------
// ImageLoader

ImageLoadData* ImageLoader::load(const std::wstring& path)
{
	std::wstring wsPath = ResourceFinder::get().find(path);
	std::string sPath;
	wstr_to_str(wsPath, sPath);
	return loadImage_internal(sPath.c_str());
}
