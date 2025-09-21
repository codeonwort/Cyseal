#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "core/vec3.h"

static std::wstring ToString(const vec3& v)
{
	wchar_t buf[256];
	swprintf_s(buf, L"(%.3f, %.3f, .%3f)", v.x, v.y, v.z);
	return std::wstring(buf);
}

namespace UnitTest
{
	TEST_CLASS(TestVector)
	{
	public:
		TEST_METHOD(EqualityAndInequality)
		{
			const wchar_t msg[] = L"TestEquality failed";

			Assert::AreEqual(vec3(0.0f), vec3(0.0f, 0.0f, 0.0f), msg);
			Assert::AreEqual(vec3(1.0f), vec3(1.0f, 1.0f, 1.0f), msg);
			Assert::AreEqual(vec3(-1.0f), -vec3(1.0f, 1.0f, 1.0f), msg);
			Assert::AreEqual(vec3(-100.0f, 200.0f, 400.0f), -vec3(100.0f, -200.0f, -400.0f), msg);

			Assert::AreNotEqual(vec3(0.0f), vec3(0.0001f), msg);
			Assert::AreNotEqual(vec3(1.0f), vec3(1.0f, 0.0f, 0.0f), msg);
			Assert::AreNotEqual(vec3(-1.0f), -vec3(1.0f, 0.0f, 0.0f), msg);
			Assert::AreNotEqual(vec3(1.0f, 2.0f, 3.0f), vec3(0.0f), msg);
			Assert::AreNotEqual(vec3(1.0f, 2.0f, 3.0f), vec3(1.0f, 2.0f, 4.0f), msg);
			Assert::AreNotEqual(vec3(1.0f, 2.0f, 3.0f), vec3(1.0f, 4.0f, 3.0f), msg);
			Assert::AreNotEqual(vec3(1.0f, 2.0f, 3.0f), vec3(4.0f, 2.0f, 3.0f), msg);
		}
	};
}
