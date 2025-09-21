#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "loader/image_loader.h"
#include "util/resource_finder.h"

namespace UnitTest
{
	TEST_CLASS(TestImageLoader)
	{
	public:
		TEST_METHOD(LoadedImageIsNotNull)
		{
			ResourceFinder::get().addBaseDirectory(L"../");
			ResourceFinder::get().addBaseDirectory(L"../../");
			ResourceFinder::get().addBaseDirectory(L"../../shaders/");
			ResourceFinder::get().addBaseDirectory(L"../../external/");

			ImageLoader loader;
			ImageLoadData* imageData = loader.load(L"external/skybox_Footballfield/negx.jpg");

			Assert::IsNotNull(imageData);
			Assert::IsNotNull(imageData->buffer);
			Assert::IsTrue(imageData->length > 0);
			Assert::IsTrue(imageData->width > 0);
			Assert::IsTrue(imageData->height > 0);
			Assert::IsTrue(imageData->numComponents > 0);

			delete imageData;
		}
	};
}
