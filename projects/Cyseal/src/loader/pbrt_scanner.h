#pragma once

#include <istream>

// #todo-pbrt: Rework pbrt scanner
#if 0
namespace pbrt
{
	enum class TokenType
	{
		// Meta tokens
		WorldBegin, AttributeBegin, AttributeEnd,
		// Before WorldBegin tokens
		LookAt, Camera, Sampler, Integrator, PixelFilter, Film,
		// After WorldBegin tokens
		LightSource, Material, NamedMaterial, MakeNamedMaterial,
		Shape, Texture, Translate, Transform, AreaLightSource,
	};

	struct Token
	{
		TokenType type;
		std::string_view value;
		int32 line;
	};
}

class PBRT4Scanner final
{
public:
	void scanTokens(std::istream& stream);

private:
	void scanToken(const std::string& tok);

	std::vector<pbrt::Token> tokens;
};
#endif
