#include "unit_test.h"
#include "core/assertion.h"

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
	for (UnitTest* test : UnitTestValidator::instance().tests)
	{
		bool result = test->runTest();
		CHECK(result);
	}
}

void UnitTestValidator::addTest(UnitTest* unitTest)
{
	tests.push_back(unitTest);
}
