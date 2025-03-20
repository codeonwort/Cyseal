#pragma once

#include "core/int_types.h"

#include <istream>
#include <string>
#include <string_view>

// #todo-pbrt: Parse PBRT file using scanner
namespace pbrt
{
	enum class TokenType
	{
		String, QuoteString, Number,
		LeftBracket, RightBracket,
	};

	struct Token
	{
		TokenType type;
		std::string_view value;
		int32 line;
	};

	class PBRT4Scanner final
	{
	public:
		void scanTokens(std::istream& stream);

		inline const std::vector<pbrt::Token>& getTokens() const { return tokens; }

	private:
		void scanLine(const std::string& line);
		void makeToken(std::istream& stream, const std::string& line, std::streampos startPos, pbrt::TokenType tokenType);
		void skipWhitespace(std::istream& stream);

		std::vector<std::string> sourceLines;
		std::vector<pbrt::Token> tokens;
		int32 currentLine = 0;
		std::streampos startPos, currentPos;
	};
}
