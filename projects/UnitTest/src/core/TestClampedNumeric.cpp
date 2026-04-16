#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "core/int_types.h"
#include "core/clamped_numeric.h"

namespace UnitTest
{
	TEST_CLASS(TestClampedNumeric)
	{
	public:
		TEST_METHOD(TestOutOfRange)
		{
			TestOutOfRangeInternal<int8>(10, -5, 20, -10, 30);
			TestOutOfRangeInternal<int16>(10, -5, 2000, -10, 3000);
			TestOutOfRangeInternal<int32>(10, -0xabcdef, 592979, -0xabcdef - 512, 592979 + 3000);
			TestOutOfRangeInternal<int64>(123456, -0xffffffabcdef, 0xffffffeeeeeeee, -0xffffffabcdef - 32, 0xffffffeeeeeeee + 10);

			TestOutOfRangeInternal<uint8>(10, 5, 20, 0, 40);
			TestOutOfRangeInternal<uint16>(512, 256, 32768, 24, 40000);
			TestOutOfRangeInternal<uint32>(0xaccdef, 0xabcdef, 0xfedcba, 0xabcdef - 512, 0xfedcba + 0xedf);
			TestOutOfRangeInternal<uint64>(0xffffffff00000000, 0x00000000ffffffff, 0xffffffffeeeeeeee, 0x00000000ffffffff - 0xffff, 0xffffffffeeeeeeee + 0xaaaa);

			TestOutOfRangeInternal<float>(2676.251f, -16232.0f, 712234.f, -30000.0f, 2000000.0f);
			TestOutOfRangeInternal<double>(2676.251, -16232.0, 712234, -30000, 2000000);
		}

	private:
		template<typename T>
		void TestOutOfRangeInternal(T x0, T xMin, T xMax, T xLessMin, T xGreaterMax)
		{
			Clamped<T> x(x0, xMin, xMax);
			Assert::IsTrue(x == x0);
			Assert::AreEqual(x.getValue(), x0);
			Assert::AreEqual(x.getMinValue(), xMin);
			Assert::AreEqual(x.getMaxValue(), xMax);

			x = xLessMin;
			Assert::IsTrue(x == x.getMinValue());
			Assert::AreEqual(x.getValue(), x.getMinValue());

			x = xGreaterMax;
			Assert::IsTrue(x == x.getMaxValue());
			Assert::AreEqual(x.getValue(), x.getMaxValue());
		}
	};
}
