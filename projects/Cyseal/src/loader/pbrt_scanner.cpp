#if 0
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

void PBRT4Scanner::scanTokens(std::istream& stream)
{
	std::vector<std::string> sourceLines;
	{
		std::string line;
		while (std::getline(stream, line))
		{
			sourceLines.emplace_back(line);
		}
	}

	int32 currentLine = 0;
	for (const std::string& line : sourceLines)
	{
		std::stringstream ss(line);
		while (!ss.eof())
		{
			std::string tok;
			ss >> tok;
			if (tok.size() == 0) break;
			scanToken(tok);
		}

		++currentLine;
	}
}

void PBRT4Scanner::scanToken(const std::string& tok)
{
	if (tok[0] == '\"')
	{
		//
	}
}
#endif
