#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "core/vec3.h"
#include "loader/image_loader.h"
#include "util/resource_finder.h"

#define STBN_DIR            L"external/NVidiaSTBNUnzippedAssets/STBN/"
#define STBN_WIDTH          128
#define STBN_HEIGHT         128
#define STBN_SLICES         64
std::wstring STBN_FILEPATH(size_t ix)
{
	wchar_t buf[256];
	swprintf_s(buf, L"%s%s%d.png", STBN_DIR, L"stbn_unitvec3_cosine_2Dx1D_128x128x64_", (int32)ix);
	return buf;
}

namespace UnitTest
{
	TEST_CLASS(TestSTBN)
	{
	public:
		TEST_METHOD(STBNUnitVector)
		{
			ResourceFinder::get().addBaseDirectory(L"../");
			ResourceFinder::get().addBaseDirectory(L"../../");
			ResourceFinder::get().addBaseDirectory(L"../../external/");
			
			uint32 numFail = 0;

			ImageLoader loader;
			for (size_t ix = 0; ix < STBN_SLICES; ++ix)
			{
				std::wstring filepath = STBN_FILEPATH(ix);
				filepath = ResourceFinder::get().find(filepath);
				Assert::AreNotEqual(filepath.size(), (size_t)0);

				ImageLoadData* blob = loader.load(filepath);

				Assert::AreEqual(blob->width, (uint32)STBN_WIDTH);
				Assert::AreEqual(blob->height, (uint32)STBN_HEIGHT);
				Assert::AreEqual(blob->numComponents, 4u);

				uint32 pixelBytes = (uint32)(blob->getRowPitch() / (blob->width * blob->numComponents));
				Assert::AreEqual(pixelBytes, 1u);

				uint8* ptr = blob->buffer;
				for (uint32 y = 0; y < STBN_HEIGHT; ++y)
				{
					for (uint32 x = 0; x < STBN_WIDTH; ++x)
					{
						float vx = (float)ptr[x * 4 + 0] / 255.0f;
						float vy = (float)ptr[x * 4 + 1] / 255.0f;
						float vz = (float)ptr[x * 4 + 2] / 255.0f;
						vx = 2.0f * vx - 1.0f;
						vy = 2.0f * vy - 1.0f;
						vz = 2.0f * vz - 1.0f;
						vec3 v(vx, vy, vz);
						
						float len = v.length();
						if (std::abs(len - 1.0f) >= 0.02f)
						{
							++numFail;
						}
					}
					ptr += blob->getRowPitch();
				}

				delete blob;
			}

			wchar_t msg[256];
			swprintf_s(msg, L"numFail = %u", numFail);
			Assert::AreEqual(numFail, 0u, msg);
		}
	};
}
