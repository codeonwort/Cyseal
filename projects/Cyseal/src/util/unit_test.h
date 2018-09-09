#pragma once

#include <vector>

class UnitTest
{

public:
	UnitTest();
	virtual bool runTest() = 0;

};

#define DEFINE_UNIT_TEST(UnitTestClass) UnitTestClass __cyseal_unit_test_##UnitTestClass;

class UnitTestValidator
{

public:
	static UnitTestValidator& instance();
	static void runAllUnitTests();

	void addTest(class UnitTest* unitTest);

private:
	UnitTestValidator() = default;

	std::vector<class UnitTest*> tests;

};
