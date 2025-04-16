#include "pbrt_scanner.h"
#include <sstream>

#define TOKEN_WORLD_BEGIN            "WorldBegin"
#define TOKEN_ATTRIBUTE_BEGIN        "AttributeBegin"
#define TOKEN_ATTRIBUTE_END          "AttributeEnd"

#define TOKEN_LOOKAT                 "LookAt"
#define TOKEN_CAMERA                 "Camera"
#define TOKEN_SAMPLER                "Sampler"
#define TOKEN_INTEGRATOR             "Integrator"
#define TOKEN_PIXEL_FILTER           "PixelFilter"
#define TOKEN_FILM                   "Film"

#define TOKEN_LIGHT_SOURCE           "LightSource"
#define TOKEN_MATERIAL               "Material"
#define TOKEN_NAMED_MATERIAL         "NamedMaterial"
#define TOKEN_MAKE_NAMED_MATERIAL    "MakeNamedMaterial"
#define TOKEN_SHAPE                  "Shape"
#define TOKEN_TEXTURE                "Texture"
#define TOKEN_TRANSLATE              "Translate"
#define TOKEN_TRANSFORM              "Transform"
#define TOKEN_AREA_LIGHT_SOURCE      "AreaLightSource"

namespace pbrt
{
	std::wstring getTokenTypeWString(TokenType tok)
	{
		switch (tok)
		{
			case TokenType::String       : return L"String";
			case TokenType::QuoteString  : return L"QuoteString";
			case TokenType::Number       : return L"Number";
			case TokenType::LeftBracket  : return L"LeftBracket";
			case TokenType::RightBracket : return L"RightBracket";
			case TokenType::EoF          : return L"EoF";
			default                      : CHECK_NO_ENTRY();
		}
		return L"";
	}

	void PBRT4Scanner::scanTokens(std::istream& stream)
	{
		sourceLines.clear();
		{
			std::string line;
			while (std::getline(stream, line))
			{
				sourceLines.emplace_back(line);
			}
		}

		currentLine = 1;
		startPos = currentPos = 0;
		for (const std::string& line : sourceLines)
		{
			scanLine(line);
			++currentLine;
		}
		
		Token eofTok;
		eofTok.type = TokenType::EoF;
		tokens.emplace_back(eofTok);
	}

	void PBRT4Scanner::scanLine(const std::string& line)
	{
		std::stringstream stream(line);
		
		for (;;)
		{
			skipWhitespace(stream);
			if (stream.eof()) break;

			std::streampos startPos = stream.tellg();
			char ch;
			stream >> ch;

			if (ch == '#')
			{
				break;
			}
			else if (ch == '-' || ch == '+' || ch == '.')
			{
				char ch2;
				stream >> ch2;
				if (std::isdigit(ch2))
				{
					stream.putback(ch2);
					stream.putback(ch);
					double num;
					stream >> num;
					makeToken(stream, line, startPos, TokenType::Number);
				}
			}
			else if (std::isdigit(ch))
			{
				stream.putback(ch);
				double num;
				stream >> num;
				makeToken(stream, line, startPos, TokenType::Number);
			}
			else if (std::isalpha(ch))
			{
				stream.putback(ch);
				std::string word;
				stream >> word;
				makeToken(stream, line, startPos, TokenType::String);
			}
			else
			{
				switch (ch)
				{
					case '[': makeToken(stream, line, startPos, TokenType::LeftBracket); break;
					case ']': makeToken(stream, line, startPos, TokenType::RightBracket); break;
					case '\"':
					{
						startPos = stream.tellg();
						while (!stream.eof())
						{
							char temp;
							stream >> temp;
							if (temp == '\"')
							{
								break;
							}
						}
						stream.seekg(stream.tellg() + std::streamoff(-1));
						makeToken(stream, line, startPos, TokenType::QuoteString);
						stream.seekg(stream.tellg() + std::streamoff(1));
						break;
					}
				}
			}
		}
	}

	void PBRT4Scanner::skipWhitespace(std::istream& stream)
	{
		char ch;
		stream >> ch;
		if (!stream.eof()) stream.putback(ch);
	}

	void pbrt::PBRT4Scanner::makeToken(std::istream& stream, const std::string& line, std::streampos startPos, pbrt::TokenType tokenType)
	{
		Token tok;
		tok.line = currentLine;
		tok.type = tokenType;
		if (stream.eof())
		{
			tok.value = std::string_view{ line.begin() + startPos, line.end() };
		}
		else
		{
			tok.value = std::string_view{ line.begin() + startPos, line.begin() + stream.tellg() };
		}
		tokens.emplace_back(tok);
	}

}