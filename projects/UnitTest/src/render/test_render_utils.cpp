#include "pch.h"
#include "test_render_utils.h"

#include "loader/image_loader.h"
#include "core/int_types.h"
#include "core/assertion.h"

#include <filesystem>
#include <string>

static std::wstring getSolutionDirectory()
{
	static std::wstring solutionDir;
	if (solutionDir.size() == 0)
	{
		std::filesystem::path currentDir = std::filesystem::current_path();
		int count = 64;
		while (count-- > 0)
		{
			auto sln = currentDir;
			sln.append("CysealSolution.sln");
			if (std::filesystem::exists(sln))
			{
				solutionDir = currentDir.wstring() + L"/";
				break;
			}
			currentDir = currentDir.parent_path();
		}
		CHECK(count >= 0); // Couldn't find shader directory
	}
	return solutionDir;
}

namespace render_test
{
	std::vector<uint8> rgba32f_to_rgba8ui(float* src, uint32 numPixels)
	{
		std::vector<uint8> dst(4 * numPixels);
		for (size_t p = 0; p < dst.size(); p += 4)
		{
			float r = src[p], g = src[p + 1], b = src[p + 2], a = src[p + 3];
			dst[p] = (uint32)(r * 255.0f) & 0xff;
			dst[p + 1] = (uint32)(g * 255.0f) & 0xff;
			dst[p + 2] = (uint32)(b * 255.0f) & 0xff;
			dst[p + 3] = (uint32)(a * 255.0f) & 0xff;
		}
		return dst;
	}

	uint32 compareRefImageToRgba8ui(const wchar_t* refImagePath, uint8* imageActual)
	{
		std::wstring solutionDir = getSolutionDirectory();
		if (solutionDir.size() > 0)
		{
			std::wstring fullPath = solutionDir + L"tests/referenceImages/" + refImagePath;

			ImageLoader loader;
			ImageLoadData* refData = loader.load(fullPath, false, false);
			if (refData != nullptr)
			{
				uint8* p1 = reinterpret_cast<uint8*>(refData->buffer);
				uint8* p2 = imageActual;
				int numDiffRows = 0;
				for (uint32 y = 0; y < refData->height; ++y)
				{
					int cmp = std::memcmp(p1, p2, refData->getRowPitch());
					if (0 != cmp) numDiffRows++;
					p1 += refData->getRowPitch();
					p2 += refData->getRowPitch();
				}
				return numDiffRows;
			}
		}
		return 0xffffffff;
	}

	uint32 compareRefImageToRgba32f(const wchar_t* refImagePath, float* imageActual)
	{
		std::wstring solutionDir = getSolutionDirectory();
		if (solutionDir.size() > 0)
		{
			std::wstring fullPath = solutionDir + L"tests/referenceImages/" + refImagePath;

			ImageLoader loader;
			ImageLoadData* refData = loader.load(fullPath, false, false);
			if (refData != nullptr)
			{
				std::vector<uint8> rgba8 = rgba32f_to_rgba8ui(imageActual, refData->width * refData->height);

				uint8* p1 = reinterpret_cast<uint8*>(refData->buffer);
				uint8* p2 = rgba8.data();
				int numDiffRows = 0;
				for (uint32 y = 0; y < refData->height; ++y)
				{
					int cmp = std::memcmp(p1, p2, refData->getRowPitch());
					if (0 != cmp) numDiffRows++;
					p1 += refData->getRowPitch();
					p2 += refData->getRowPitch();
				}
				return numDiffRows;
			}
		}
		return 0xffffffff;
	}

	bool saveRgba8uiImage(const wchar_t* filepath, uint8* rgba8Image, uint32 width, uint32 height)
	{
		std::wstring solutionDir = getSolutionDirectory();
		if (solutionDir.size() > 0)
		{
			std::wstring fullPath = solutionDir + L"intermediate/testResults/" + filepath;
			int rowPitch = width * 4;
			bool bRet = ImageLoader::saveAsPng(fullPath, rgba8Image, width, height, rowPitch);
			return bRet;
		}
		return false;
	}

	bool saveRgba32fImage(const wchar_t* filepath, float* rgba32fImage, uint32 width, uint32 height)
	{
		std::vector<uint8> rgba8 = rgba32f_to_rgba8ui(rgba32fImage, width * height);
		return saveRgba8uiImage(filepath, rgba8.data(), width, height);
	}
}
