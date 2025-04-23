#pragma once

#include "core/int_types.h"

#include <istream>
#include <vector>
#include <string>
#include <string_view>

namespace pbrt
{
	enum class TokenType
	{
		String, QuoteString, Number,
		LeftBracket, RightBracket,
		EoF, // Having an EOF token is more convenient than dealing with std iterator end
	};

	std::wstring getTokenTypeWString(TokenType tok);

	struct Token
	{
		TokenType type;
		std::string_view value;
		int32 line;
	};

	/// <summary>
	/// Reads pbrt4 files recursively, processing Include directives.
	/// </summary>
	/// <param name="filepath">Full filepath to open</param>
	/// <param name="outSourceLines">All file contents combined</param>
	/// <returns>true if all files were open successfully. Does not check if the content has valid pbrt4 format.</returns>
	bool readFileRecursive(const wchar_t* filepath, std::vector<std::string>& outSourceLines);

	// Reads pbrt4 file and generates tokens which can be recognized by PBRT4Parser.
	class PBRT4Scanner final
	{
	public:
		void scanTokens(std::istream& stream);
		void scanTokens(const std::vector<std::string>& lines);

		inline const std::vector<pbrt::Token>& getTokens() const { return tokens; }

	private:
		void scanTokensSub();
		void scanLine(const std::string& line);
		void makeToken(std::istream& stream, const std::string& line, std::streampos startPos, pbrt::TokenType tokenType);
		void skipWhitespace(std::istream& stream);

		std::vector<std::string> sourceLines;
		std::vector<pbrt::Token> tokens;
		int32 currentLine = 0;
		std::streampos startPos, currentPos;
	};
}
