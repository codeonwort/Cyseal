#include "util/unit_test.h"
// #todo-crossplatform: I'm including Windows.h somewhere in engine headers. Need to exclude it.
#include <Windows.h>
#include "util/logging.h"
#include "core/vec3.h"
#include "loader/image_loader.h"

class UnitTestHello : public UnitTest
{
	virtual bool runTest() override
	{
		return true;
	}
};
DEFINE_UNIT_TEST(UnitTestHello);

class UnitTestVector : public UnitTest
{
	virtual bool runTest() override
	{
		if (vec3(0.0f, 0.0f, 0.0f) != vec3(0.0f, 0.0f, 0.0f))
		{
			return false;
		}
		return true;
	}
};
DEFINE_UNIT_TEST(UnitTestVector);

DEFINE_LOG_CATEGORY_STATIC(LogTemp);
class UnitTestImageLoader : public UnitTest
{
	virtual bool runTest() override
	{
		ImageLoader loader;
		ImageLoadData loadData;
		bool success = loader.load(L"bee.png", loadData);
		CYLOG(LogTemp, Log, TEXT("Test image loader: %s"), success ? TEXT("Success") : TEXT("Failed"));
		return true;
	}
};
DEFINE_UNIT_TEST(UnitTestImageLoader);
