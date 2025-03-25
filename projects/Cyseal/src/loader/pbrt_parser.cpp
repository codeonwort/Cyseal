#include "pbrt_parser.h"
#include "core/assertion.h"
#include <sstream>

// https://pbrt.org/fileformat-v4

#define DIRECTIVE_WORLD_BEGIN            "WorldBegin"
#define DIRECTIVE_ATTRIBUTE_BEGIN        "AttributeBegin"
#define DIRECTIVE_ATTRIBUTE_END          "AttributeEnd"

#define DIRECTIVE_LOOKAT                 "LookAt"
#define DIRECTIVE_CAMERA                 "Camera"
#define DIRECTIVE_SAMPLER                "Sampler"
#define DIRECTIVE_INTEGRATOR             "Integrator"
#define DIRECTIVE_PIXEL_FILTER           "PixelFilter"
#define DIRECTIVE_FILM                   "Film"

#define DIRECTIVE_LIGHT_SOURCE           "LightSource"
#define DIRECTIVE_MATERIAL               "Material"
#define DIRECTIVE_NAMED_MATERIAL         "NamedMaterial"
#define DIRECTIVE_MAKE_NAMED_MATERIAL    "MakeNamedMaterial"
#define DIRECTIVE_SHAPE                  "Shape"
#define DIRECTIVE_TEXTURE                "Texture"
#define DIRECTIVE_TRANSLATE              "Translate"
#define DIRECTIVE_TRANSFORM              "Transform"
#define DIRECTIVE_AREA_LIGHT_SOURCE      "AreaLightSource"

// Legacy tokens (pbrt-v3)
#define DIRECTIVE_TRANSFORM_BEGIN        "TransformBegin"
#define DIRECTIVE_TRANSFORM_END          "TransformEnd"

// #todo-pbrt-parser: Replace with proper error handling
#define PARSER_CHECK CHECK
#define PARSER_CHECK_NO_ENTRY CHECK_NO_ENTRY

namespace pbrt
{
	void PBRT4ParserEx::parse(PBRT4Scanner* scanner)
	{
		parsePhase = PBRT4ParsePhase::RenderingOptions;

		const std::vector<Token>& tokens = scanner->getTokens();
		auto it = tokens.begin();
		while (it->type != TokenType::EoF)
		{
			directive(it);
		}
	}

	void PBRT4ParserEx::directive(TokenIter& it)
	{
		if (it->type == TokenType::String)
		{
			// #todo-pbrt-parser: Replace if-else chain with function pointer table?
			if (it->value == DIRECTIVE_WORLD_BEGIN)
			{
				PARSER_CHECK(parsePhase == PBRT4ParsePhase::RenderingOptions);
				parsePhase = PBRT4ParsePhase::SceneElements;
				++it;
			}
			else if (it->value == DIRECTIVE_INTEGRATOR)
			{
				++it;
				integrator(it);
			}
			else if (it->value == DIRECTIVE_TRANSFORM)
			{
				++it;
				transform(it);
			}
		}
		else
		{
			PARSER_CHECK_NO_ENTRY();
		}
	}

	void PBRT4ParserEx::integrator(TokenIter& it)
	{
		struct Integrator
		{
			std::string name;

			int32 maxDepth             = 5;       // All but "ambientocclusion"
			std::string lightsampler   = "bvh";   // "path", "volpath", wavefront/GPU
			bool regularize            = false;   // "bdpt", "mlt", "path", "volatph", wavefront/GPU
			bool cossample             = true;    // "ambientocclusion"
			float maxdistance          = FLT_MAX; // "ambientocclusion"
			bool visualizestrategies   = false;   // "bdpt"
			bool visualizeweights      = false;   // "bdpt"
			int32 bootstrapsamples     = 100000;  // "mlt"
			int32 chains               = 1000;    // "mlt"
			int32 mutationsperpixel    = 100;     // "mlt"
			float pargestepprobability = 0.3f;    // "mlt"
			float sigma                = 0.01f;   // "mlt"
			float samplebsdf           = true;    // "simplepath"
			float samplelights         = true;    // "simplepath"
			int32 photonsperiteration  = -1;      // "sppm"
			float radius               = 1.0f;    // "sppm"
			int32 seed                 = 0;       // "sppm"
		};

		// "ambientocclusion" Ambient occlusion (accessibility over the hemisphere)
		// "bdpt"			  Bidirectional path tracing
		// "lightpath"		  Path tracing starting from the light sources
		// "mlt"			  Metropolis light transport using bidirectional path tracing
		// "path"			  Path tracing
		// "randomwalk"		  Rendering using a simple random walk without any explicit light sampling
		// "simplepath"		  Path tracing with very basic sampling algorithms
		// "simplevolpath"	  Volumetric path tracing with very basic sampling algorithms
		// "sppm"			  Stochastic progressive photon mapping
		// "volpath"		  Volumetric path tracing
		static const std::vector<std::string> validNames = {
			"ambientocclusion", "bdpt", "lightpath", "mlt", "path", "randomwalk",
			"simplepath", "simplevolpath", "sppm", "volpath"
		};

		Integrator integratorDesc{};

		if (it->type == TokenType::QuoteString)
		{
			std::string_view integratorName = it->value;
			auto nameIt = std::find(validNames.begin(), validNames.end(), integratorName);
			if (nameIt != validNames.end())
			{
				integratorDesc.name = *nameIt;
				++it;
				parameters(it);
			}
			else
			{
				PARSER_CHECK_NO_ENTRY();
			}
		}
		else
		{
			PARSER_CHECK_NO_ENTRY();
		}
	}

	void PBRT4ParserEx::parameters(TokenIter& it)
	{
		while (it->type == TokenType::QuoteString)
		{
			std::stringstream ss(it->value.data());
			std::string ptype, pname;
			ss >> ptype >> pname;
			PARSER_CHECK(ptype.size() != 0 && pname.size() > 1);
			pname = pname.substr(0, pname.size() - 1);
			++it;
			if (ptype == "integer")
			{
				PARSER_CHECK(it->type == TokenType::LeftBracket);
				++it;
				PARSER_CHECK(it->type == TokenType::Number);
				int32 value = std::stoi(it->value.data());
				// #todo-pbrt-parser: Emit parsed integer parameter
				++it;
				PARSER_CHECK(it->type == TokenType::RightBracket);
				++it;
			}
			else if (ptype == "float")
			{
				PARSER_CHECK(it->type == TokenType::LeftBracket);
				++it;
				PARSER_CHECK(it->type == TokenType::Number);
				float value = std::stof(it->value.data());
				// #todo-pbrt-parser: Emit parsed float parameter
				++it;
				PARSER_CHECK(it->type == TokenType::RightBracket);
				++it;
			}
			else if (ptype == "rgb")
			{
				PARSER_CHECK(it->type == TokenType::LeftBracket);

				// #todo-pbrt-parser: Emit parsed rgb parameter
				++it;
				PARSER_CHECK(it->type == TokenType::Number);
				float R = std::stof(it->value.data());
				
				++it;
				PARSER_CHECK(it->type == TokenType::Number);
				float G = std::stof(it->value.data());

				++it;
				PARSER_CHECK(it->type == TokenType::Number);
				float B = std::stof(it->value.data());

				++it;
				PARSER_CHECK(it->type == TokenType::RightBracket);
				++it;
			}
			else
			{
				// #todo-pbrt-parser: Other parameter types
				CHECK_NO_ENTRY();
			}
		}
	}

	void PBRT4ParserEx::transform(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::LeftBracket);
		++it;

		Matrix mat;
		for (size_t i = 0; i < 16; ++i)
		{
			PARSER_CHECK(it->type == TokenType::Number);
			float value = std::stof(it->value.data());
			mat.m[i / 4][i % 4] = value;
			++it;
		}
		PARSER_CHECK(it->type == TokenType::RightBracket);
		++it;

		// #todo-pbrt-parser: Emit parsed transform
	}

}
