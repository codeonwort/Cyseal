#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "loader/pbrt_scanner.h"
#include "loader/pbrt_parser.h"
#include "util/resource_finder.h"

#include <vector>
#include <string>
#include <fstream>
#include <numeric>

const std::vector<std::string> sourceLines = {
	"Integrator \"path\" # some comment",
	"\"integer maxdepth\" [ 65 ]",
	"#qwer wee        ",
	"Transform [ 0.999914 0.000835626 0.013058 -0 -0 0.997959 -0.063863 -0 0.0130847 -0.0638576 -0.997873 -0 0.460159 -2.13584 9.87771 1  ]",
};

const std::vector<std::string> sourceLines_wrongDirectiveFormat = {
	"[Integrator] \"path\" # some comment",
	"\"integer maxdepth\" [ 65 ]",
	"#qwer wee        ",
	"Transform [ 0.999914 0.000835626 0.013058 -0 -0 0.997959 -0.063863 -0 0.0130847 -0.0638576 -0.997873 -0 0.460159 -2.13584 9.87771 1  ]",
};
const std::vector<std::string> sourceLines_wrongDirectiveName = {
	"Integrator \"path\" # some comment",
	"\"integer maxdepth\" [ 65 ]",
	"#qwer wee        ",
	"Transform123 [ 0.999914 0.000835626 0.013058 -0 -0 0.997959 -0.063863 -0 0.0130847 -0.0638576 -0.997873 -0 0.460159 -2.13584 9.87771 1  ]",
};

//#define PBRT_FILEPATH L"external/pbrt4_dining_room/dining-room/scene-v4.pbrt"
#define PBRT_FILEPATH L"external/pbrt4_bedroom/bedroom/scene-v4.pbrt"
//#define PBRT_FILEPATH L"external/pbrt4_house/house/scene-v4.pbrt"

namespace UnitTest
{
	TEST_CLASS(TestPBRTParser)
	{
	public:
		TEST_METHOD(TestScanner)
		{
			std::stringstream sourceStream;
			for (const auto& line : sourceLines)
			{
				sourceStream << line << std::endl;
			}

			pbrt::PBRT4Scanner scanner;
			scanner.scanTokens(sourceStream);
			const auto& tokens = scanner.getTokens();

			Assert::IsTrue(pbrt::TokenType::String == tokens[0].type);
			Assert::IsTrue("Integrator" == tokens[0].value);

			Assert::IsTrue(pbrt::TokenType::QuoteString == tokens[1].type);
			Assert::IsTrue("path" == tokens[1].value);

			Assert::IsTrue(pbrt::TokenType::QuoteString == tokens[2].type);
			Assert::IsTrue("integer maxdepth" == tokens[2].value);

			Assert::IsTrue(pbrt::TokenType::LeftBracket == tokens[3].type);
			Assert::IsTrue("[" == tokens[3].value);

			Assert::IsTrue(pbrt::TokenType::Number == tokens[4].type);
			Assert::IsTrue("65" == tokens[4].value);

			Assert::IsTrue(pbrt::TokenType::RightBracket == tokens[5].type);
			Assert::IsTrue("]" == tokens[5].value);

			Assert::IsTrue(pbrt::TokenType::String == tokens[6].type);
			Assert::IsTrue("Transform" == tokens[6].value);

			Assert::IsTrue(pbrt::TokenType::LeftBracket == tokens[7].type);
			Assert::IsTrue("[" == tokens[7].value);

			size_t matrixStart = 8;
			for (size_t i = matrixStart; i < matrixStart + 16; ++i)
			{
				Assert::IsTrue(pbrt::TokenType::Number == tokens[i].type);
			}

			Assert::IsTrue(pbrt::TokenType::RightBracket == tokens[matrixStart + 16].type);
			Assert::IsTrue("]" == tokens[matrixStart + 16].value);

			pbrt::PBRT4Parser parser;
			pbrt::PBRT4ParserOutput parserOutput = parser.parse(&scanner);
			Assert::IsTrue(parserOutput.bValid, L"Parser reported errors");
		}

		TEST_METHOD(TestParser)
		{
			std::stringstream sourceStream;
			for (const auto& line : sourceLines)
			{
				sourceStream << line << std::endl;
			}

			pbrt::PBRT4Scanner scanner;
			scanner.scanTokens(sourceStream);

			pbrt::PBRT4Parser parser;
			pbrt::PBRT4ParserOutput parserOutput = parser.parse(&scanner);
			Assert::IsTrue(parserOutput.bValid, L"Parser reported errors");
		}

		TEST_METHOD(TestParserFailure1)
		{
			std::stringstream sourceStream;
			for (const auto& line : sourceLines_wrongDirectiveFormat)
			{
				sourceStream << line << std::endl;
			}

			pbrt::PBRT4Scanner scanner;
			scanner.scanTokens(sourceStream);

			pbrt::PBRT4Parser parser;
			pbrt::PBRT4ParserOutput parserOutput = parser.parse(&scanner);
			Assert::IsFalse(parserOutput.bValid, L"Parser didn't report errors for invalid input");
		}

		TEST_METHOD(TestParserFailure2)
		{
			std::stringstream sourceStream;
			for (const auto& line : sourceLines_wrongDirectiveName)
			{
				sourceStream << line << std::endl;
			}

			pbrt::PBRT4Scanner scanner;
			scanner.scanTokens(sourceStream);

			pbrt::PBRT4Parser parser;
			pbrt::PBRT4ParserOutput parserOutput = parser.parse(&scanner);
			Assert::IsFalse(parserOutput.bValid, L"Parser didn't report errors for invalid input");
		}

		TEST_METHOD(TestParserWithFile)
		{
			ResourceFinder::get().addBaseDirectory(L"../");
			ResourceFinder::get().addBaseDirectory(L"../../");
			ResourceFinder::get().addBaseDirectory(L"../../external/");

			std::wstring wFilepath = ResourceFinder::get().find(PBRT_FILEPATH);
			Assert::IsTrue(wFilepath.size() != 0, L"Can't find the file");

			std::fstream fs(wFilepath);
			Assert::IsTrue(!!fs, L"Can't open a file stream");

			pbrt::PBRT4Scanner scanner;
			scanner.scanTokens(fs);

			pbrt::PBRT4Parser parser;
			pbrt::PBRT4ParserOutput parserOutput = parser.parse(&scanner);
			Assert::IsTrue(parserOutput.bValid, L"Parser reported errors");
		}
	};
}
