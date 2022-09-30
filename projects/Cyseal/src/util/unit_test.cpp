#include "unit_test.h"
#include "core/assertion.h"
#include "core/critical_section.h"
#include "util/logging.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnitTest);

UnitTest::UnitTest()
{
	UnitTestValidator::instance().addTest(this);
}

UnitTestValidator& UnitTestValidator::instance()
{
	static UnitTestValidator inst;
	return inst;
}

void UnitTestValidator::runAllUnitTests()
{
	size_t numTests = UnitTestValidator::instance().tests.size();
	for (UnitTest* test : UnitTestValidator::instance().tests)
	{
		bool result = test->runTest();
		if (!result)
		{
			__debugbreak();
			CYLOG(LogUnitTest, Fatal, TEXT("Unit test failed"));
		}
		CHECK(result);
	}
	CYLOG(LogUnitTest, Log, TEXT("All unit tests have passed. Count: %u"), numTests);
}

void UnitTestValidator::addTest(UnitTest* unitTest)
{
	static CriticalSection CS;
	{
		CS.Enter();
		tests.push_back(unitTest);
		CS.Leave();
	}
}
