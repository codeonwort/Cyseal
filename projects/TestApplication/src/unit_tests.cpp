#include "util/unit_test.h"
#include "util/logging.h"
#include "core/vec3.h"
#include "core/assertion.h"
#include "memory/free_number_list.h"
#include "loader/image_loader.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnitTest);

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

class UnitTestImageLoader : public UnitTest
{
	virtual bool runTest() override
	{
		ImageLoader loader;
		ImageLoadData* loadData = loader.load(L"bee.png");
		CYLOG(LogUnitTest, Log, L"Test image loader: %s", loadData != nullptr ? L"Success" : L"Failed");
		return true;
	}
};
DEFINE_UNIT_TEST(UnitTestImageLoader);

class UnitTestFreeNumber : public UnitTest
{
	virtual bool runTest() override
	{
		FreeNumberList fn(10);
		uint32 n1 = fn.allocate();
		uint32 n2 = fn.allocate();
		uint32 n3 = fn.allocate();
		CYLOG(LogUnitTest, Log, L"n1: %u", n1);
		CYLOG(LogUnitTest, Log, L"n2: %u", n2);
		CYLOG(LogUnitTest, Log, L"n3: %u", n3);
		CHECK(true == fn.deallocate(n1));
		CHECK(false == fn.deallocate(10));
		CYLOG(LogUnitTest, Log, L"Return n1(%u)", n1);
		uint32 n4 = fn.allocate();
		uint32 n5 = fn.allocate();
		uint32 n6 = fn.allocate();
		uint32 n7 = fn.allocate();
		CYLOG(LogUnitTest, Log, L"n4: %u", n4);
		CYLOG(LogUnitTest, Log, L"n5: %u", n5);
		CYLOG(LogUnitTest, Log, L"n6: %u", n6);
		CYLOG(LogUnitTest, Log, L"n7: %u", n7);
		CHECK(true == fn.deallocate(n5));
		CHECK(true == fn.deallocate(n6));
		CYLOG(LogUnitTest, Log, L"Return n5(%u), n6(%u)", n5, n6);
		uint32 n8 = fn.allocate();
		CYLOG(LogUnitTest, Log, L"n8: %u", n8);

		return true;
	}
};
DEFINE_UNIT_TEST(UnitTestFreeNumber);
