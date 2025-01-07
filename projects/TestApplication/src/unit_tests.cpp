#include "util/unit_test.h"
#include "util/logging.h"
#include "core/assertion.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnitTest);

// Unit tests are moved to a separate project that uses Microsoft's CppUnitTestFramework.
// I'll keep my custom framework here, in case of functional tests in future might need it.
class UnitTestDummy : public UnitTest
{
	virtual bool runTest() override
	{
		return true;
	}
};
DEFINE_UNIT_TEST(UnitTestDummy);
