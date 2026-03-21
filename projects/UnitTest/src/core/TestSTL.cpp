#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include <sstream>
#include <string>

namespace UnitTest
{
	TEST_CLASS(TestSTL)
	{
	public:
		TEST_METHOD(SStreamSkipWhitespace)
		{
			std::string str = "asd   zxc";
			std::stringstream stream(str);

			std::string s1, s2;
			stream >> s1;
			Assert::AreEqual(s1, std::string("asd"));

			char ch;
			stream >> ch;
			if (!stream.eof()) stream.putback(ch);
			Assert::AreEqual(6, (int)stream.tellg());

			stream >> s2;
			Assert::AreEqual(s2, std::string("zxc"));
		}
	};
}
