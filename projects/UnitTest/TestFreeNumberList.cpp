#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "memory/free_number_list.h"

// Current implementation returns a smallest possible number but it's not guaranteed by spec.
#define CHECK_SMALLEST_POSSIBLE_NUMBER 1

namespace UnitTest
{
	TEST_CLASS(TestFreeNumberList)
	{
	public:
		TEST_METHOD(AllocateFreeNumberList)
		{
			FreeNumberList fn(10);
			uint32 n1 = fn.allocate();
			uint32 n2 = fn.allocate();
			uint32 n3 = fn.allocate();
#if CHECK_SMALLEST_POSSIBLE_NUMBER
			Assert::AreEqual(n1, 1u);
			Assert::AreEqual(n2, 2u);
			Assert::AreEqual(n3, 3u);
#endif
			Assert::IsTrue(fn.deallocate(n1), L"Failed to deallocate a valid number");
			Assert::IsFalse(fn.deallocate(10), L"Succeeded to deallocate an invalid number");
			uint32 n4 = fn.allocate();
			uint32 n5 = fn.allocate();
			uint32 n6 = fn.allocate();
			uint32 n7 = fn.allocate();
#if CHECK_SMALLEST_POSSIBLE_NUMBER
			Assert::AreEqual(n4, 1u); // n1 = 1 was deallocated, so should be n4 = 1
			Assert::AreEqual(n5, 4u);
			Assert::AreEqual(n6, 5u);
			Assert::AreEqual(n7, 6u);
#endif
			Assert::IsTrue(fn.deallocate(n5), L"Failed to deallocate a valid number");
			Assert::IsTrue(fn.deallocate(n6), L"Failed to deallocate a valid number");
			uint32 n8 = fn.allocate();
#if CHECK_SMALLEST_POSSIBLE_NUMBER
			Assert::AreEqual(n8, 4u);
#endif
		}
	};
}
