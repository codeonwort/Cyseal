#include "image_loader.h"
#include "util/string_conversion.h"
#include "util/resource_finder.h"

#include <filesystem>

static void ensureDirectory(const std::wstring& path)
{
	std::filesystem::path p(path);
	std::filesystem::create_directories(p.parent_path());
}

// ------------------------------------------------
// <stb_image> wrapper
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define __STDC_LIB_EXT1__
#include <stb_image_write.h>

ImageLoadData* loadImage_internal(char const* filename, bool flipY)
{
	const int numRequiredComps = 4; // RGB-only data cannot be directly uploaded for RGBA8 formats.
	int width, height, numActualComponents;

	if (flipY) stbi_set_flip_vertically_on_load(true);
	unsigned char* buffer = ::stbi_load(filename, &width, &height, &numActualComponents, numRequiredComps);
	if (flipY) stbi_set_flip_vertically_on_load(false);
	
	uint32 numComponents = (std::max)(numRequiredComps, numActualComponents);

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

ImageLoadData* ImageLoader::load(const std::wstring& path, bool flipY, bool useResourceFinder)
{
	std::wstring wsPath = useResourceFinder ? ResourceFinder::get().find(path) : path;
	std::string sPath;
	wstr_to_str(wsPath, sPath);
	return loadImage_internal(sPath.c_str(), flipY);
}

bool ImageLoader::saveAsPng(const std::wstring& wPath, void* rgba8Data, uint32 width, uint32 height, uint64 rowPitch)
{
	if (rowPitch == 0) rowPitch = width * 4;

	ensureDirectory(wPath);

	std::string sPath;
	wstr_to_str(wPath, sPath);
	int ret = stbi_write_png(sPath.c_str(), (int)width, (int)height, 4, rgba8Data, (int)rowPitch);
	return ret != 0;
}
