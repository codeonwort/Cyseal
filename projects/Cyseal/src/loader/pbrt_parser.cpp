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
#define DIRECTIVE_ROTATE                 "Rotate"
#define DIRECTIVE_TRANSFORM              "Transform"
#define DIRECTIVE_CONCAT_TRANSFORM       "ConcatTransform"
#define DIRECTIVE_AREA_LIGHT_SOURCE      "AreaLightSource"

// Legacy tokens (pbrt-v3)
#define DIRECTIVE_TRANSFORM_BEGIN        "TransformBegin"
#define DIRECTIVE_TRANSFORM_END          "TransformEnd"

// #todo-pbrt-parser: Replace with proper error handling
#define PARSER_CHECK CHECK
#define PARSER_CHECK_NO_ENTRY CHECK_NO_ENTRY

namespace pbrt
{

	PBRT4ParserEx::PBRT4ParserEx()
	{
		directiveTable = {
			{DIRECTIVE_INTEGRATOR, std::bind(&PBRT4ParserEx::integrator, this, std::placeholders::_1)},
			{DIRECTIVE_TRANSFORM, std::bind(&PBRT4ParserEx::transform, this, std::placeholders::_1)},
			{DIRECTIVE_SAMPLER, std::bind(&PBRT4ParserEx::sampler, this, std::placeholders::_1)},
			{DIRECTIVE_PIXEL_FILTER, std::bind(&PBRT4ParserEx::pixelFilter, this, std::placeholders::_1)},
			{DIRECTIVE_FILM, std::bind(&PBRT4ParserEx::film, this, std::placeholders::_1)},
			{DIRECTIVE_CAMERA, std::bind(&PBRT4ParserEx::camera, this, std::placeholders::_1)},
			{DIRECTIVE_TEXTURE, std::bind(&PBRT4ParserEx::texture, this, std::placeholders::_1)},
			{DIRECTIVE_MAKE_NAMED_MATERIAL, std::bind(&PBRT4ParserEx::makeNamedMaterial, this, std::placeholders::_1)},
			{DIRECTIVE_SHAPE, std::bind(&PBRT4ParserEx::shape, this, std::placeholders::_1)},
			{DIRECTIVE_NAMED_MATERIAL, std::bind(&PBRT4ParserEx::namedMaterial, this, std::placeholders::_1)},
			{DIRECTIVE_LIGHT_SOURCE, std::bind(&PBRT4ParserEx::lightSource, this, std::placeholders::_1)},
			{DIRECTIVE_ROTATE, std::bind(&PBRT4ParserEx::rotate, this, std::placeholders::_1)},
			{DIRECTIVE_CONCAT_TRANSFORM, std::bind(&PBRT4ParserEx::concatTransform, this, std::placeholders::_1)},
		};
	}

	void PBRT4ParserEx::parse(PBRT4Scanner* scanner)
	{
		parsePhase = PBRT4ParsePhase::RenderingOptions;
		currentTransform.identity();
		bCurrentTransformIsIdentity = true;

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
				currentTransform.identity();
				currentTransformBackup.identity();
				++it;
			}
			else if (it->value == DIRECTIVE_TRANSFORM_BEGIN)
			{
				PARSER_CHECK(parsePhase == PBRT4ParsePhase::SceneElements);
				parsePhase = PBRT4ParsePhase::InsideAttribute;
				++it;
			}
			else if (it->value == DIRECTIVE_TRANSFORM_END)
			{
				PARSER_CHECK(parsePhase == PBRT4ParsePhase::InsideAttribute);
				parsePhase = PBRT4ParsePhase::SceneElements;
				currentTransform.identity();
				bCurrentTransformIsIdentity = true;
				++it;
			}
			else if (it->value == DIRECTIVE_ATTRIBUTE_BEGIN)
			{
				PARSER_CHECK(parsePhase == PBRT4ParsePhase::SceneElements);
				parsePhase = PBRT4ParsePhase::InsideAttribute;
				currentTransformBackup = currentTransform;
				++it;
			}
			else if (it->value == DIRECTIVE_ATTRIBUTE_END)
			{
				PARSER_CHECK(parsePhase == PBRT4ParsePhase::InsideAttribute);
				parsePhase = PBRT4ParsePhase::SceneElements;
				currentTransform = currentTransformBackup;
				bCurrentTransformIsIdentity = true;
				++it;
			}
			else
			{
				auto callbackIt = directiveTable.find(std::string(it->value));
				if (callbackIt != directiveTable.end())
				{
					++it;
					callbackIt->second(it);
				}
				else
				{
					PARSER_CHECK_NO_ENTRY();
				}
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

				auto params = parameters(it);
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

	std::vector<PBRT4ParameterEx> PBRT4ParserEx::parameters(TokenIter& it)
	{
		std::vector<PBRT4ParameterEx> params;

		while (it->type == TokenType::QuoteString)
		{
			std::stringstream ss(it->value.data());
			std::string ptype, pname;
			ss >> ptype >> pname;
			PARSER_CHECK(ptype.size() != 0 && pname.size() > 1);
			pname = pname.substr(0, pname.size() - 1);

			++it;
			bool hasBrackets = it->type == TokenType::LeftBracket;
			if (hasBrackets) ++it;

			if (ptype == "integer")
			{
				std::vector<int32> values;
				while (it->type == TokenType::Number)
				{
					int32 value = std::stoi(it->value.data());
					values.emplace_back(value);
					++it;
				}
				PARSER_CHECK(values.size() > 0);
				PARSER_CHECK(values.size() == 1 || hasBrackets);

				if (values.size() == 1)
				{
					if (hasBrackets) --it;

					PBRT4ParameterEx param{};
					param.datatype = PBRT4ParameterTypeEx::Int;
					param.name = pname;
					param.asInt = values[0];
					params.emplace_back(param);
				}
				else
				{
					--it;

					PBRT4ParameterEx param{};
					param.datatype = PBRT4ParameterTypeEx::IntArray;
					param.name = pname;
					param.asIntArray = std::move(values);
					params.emplace_back(param);
				}
			
			}
			else if (ptype == "float")
			{
				PARSER_CHECK(it->type == TokenType::Number);
				float value = std::stof(it->value.data());

				PBRT4ParameterEx param{};
				param.datatype = PBRT4ParameterTypeEx::Int;
				param.name = pname;
				param.asFloat = value;
				params.emplace_back(param);
			}
			else if (ptype == "rgb")
			{
				PARSER_CHECK(hasBrackets); // Only single value parameters can omit brackets.

				PARSER_CHECK(it->type == TokenType::Number);
				float R = std::stof(it->value.data());
				
				++it;
				PARSER_CHECK(it->type == TokenType::Number);
				float G = std::stof(it->value.data());

				++it;
				PARSER_CHECK(it->type == TokenType::Number);
				float B = std::stof(it->value.data());

				PBRT4ParameterEx param{};
				param.datatype = PBRT4ParameterTypeEx::Int;
				param.name = pname;
				param.asFloat3[0] = R;
				param.asFloat3[1] = G;
				param.asFloat3[2] = B;
				params.emplace_back(param);
			}
			else if (ptype == "string")
			{
				PARSER_CHECK(it->type == TokenType::QuoteString);
				std::string strValue(it->value);

				PBRT4ParameterEx param{};
				param.datatype = PBRT4ParameterTypeEx::String;
				param.name = pname;
				param.asString = std::move(strValue);
				params.emplace_back(param);
			}
			else if (ptype == "bool")
			{
				PARSER_CHECK(it->type == TokenType::String);
				std::string str(it->value);
				PARSER_CHECK(str == "true" || str == "false");

				PBRT4ParameterEx param{};
				param.datatype = PBRT4ParameterTypeEx::Bool;
				param.name = pname;
				param.asBool = (str == "true");
				params.emplace_back(param);
			}
			else if (ptype == "texture")
			{
				PARSER_CHECK(it->type == TokenType::QuoteString);
				std::string textureName(it->value);

				PBRT4ParameterEx param{};
				param.datatype = PBRT4ParameterTypeEx::Bool;
				param.name = pname;
				param.asTexture = textureName;
				params.emplace_back(param);
			}
			else if (ptype == "point2" || ptype == "vector2")
			{
				PARSER_CHECK(hasBrackets); // Only single value parameters can omit brackets.

				std::vector<float> float2Array;

				while (it->type == TokenType::Number)
				{
					float num = std::stof(it->value.data());
					float2Array.emplace_back(num);
					++it;
				}
				--it;
				PARSER_CHECK(float2Array.size() % 2 == 0);

				PBRT4ParameterEx param{};
				param.datatype = PBRT4ParameterTypeEx::Float2Array;
				param.name = pname;
				param.asFloatArray = std::move(float2Array);
				params.emplace_back(param);
			}
			// #todo-pbrt-parser: File format spec says "normal3" but actual files use "normal"?
			else if (ptype == "normal" || ptype == "point3" || ptype == "vector3")
			{
				PARSER_CHECK(hasBrackets); // Only single value parameters can omit brackets.

				std::vector<float> float3Array;

				while (it->type == TokenType::Number)
				{
					float num = std::stof(it->value.data());
					float3Array.emplace_back(num);
					++it;
				}
				--it;
				PARSER_CHECK(float3Array.size() % 3 == 0);

				PBRT4ParameterEx param{};
				param.datatype = PBRT4ParameterTypeEx::Float3Array;
				param.name = pname;
				param.asFloatArray = std::move(float3Array);
				params.emplace_back(param);
			}
			else
			{
				// #todo-pbrt-parser: Other parameter types
				CHECK_NO_ENTRY();
			}

			if (hasBrackets)
			{
				++it;
				PARSER_CHECK(it->type == TokenType::RightBracket);
			}
			++it;
		}

		return params;
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

		if (parsePhase == PBRT4ParsePhase::RenderingOptions)
		{
			// #todo-pbrt-parser: Emit parsed Transform as the scene transform
		}
		else if (parsePhase == PBRT4ParsePhase::InsideAttribute)
		{
			// #todo-pbrt-parser: Emit parsed Transform
			currentTransform = mat;
		}
	}

	void PBRT4ParserEx::sampler(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		const std::string samplerName(it->value);
		
		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed sampler
	}

	void PBRT4ParserEx::pixelFilter(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		const std::string pixelFilterName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed pixelFilter
	}

	void PBRT4ParserEx::film(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		const std::string filmName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed film
	}

	void PBRT4ParserEx::camera(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		// "orthographic", "perspective", "realistic", "spherical"
		const std::string cameraType(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed camera
	}

	void PBRT4ParserEx::texture(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);
		const std::string textureName(it->value);

		++it;
		PARSER_CHECK(it->type == TokenType::QuoteString);
		const std::string textureType(it->value);

		++it;
		PARSER_CHECK(it->type == TokenType::QuoteString);
		const std::string textureClass(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed texture
	}

	void PBRT4ParserEx::makeNamedMaterial(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);
		const std::string materialName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed MakeNamedMaterial
	}

	void PBRT4ParserEx::shape(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);
		const std::string shapeName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed Shape
	}

	void PBRT4ParserEx::namedMaterial(TokenIter& it)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);
		const std::string materialName(it->value);

		currentNamedMaterial = materialName;

		++it;
		// #todo-pbrt-parser: Emit parsed NamedMaterial
	}

	void PBRT4ParserEx::lightSource(TokenIter& it)
	{
		// "distant", "goniometric", "infinite", "point", "projection", "spot"
		PARSER_CHECK(it->type == TokenType::QuoteString);
		const std::string lightName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed LightSource
	}

	void PBRT4ParserEx::rotate(TokenIter& it)
	{
		// Rotate angle x y z
		// where angle is in degrees and (x, y, z) = axis

		PARSER_CHECK(it->type == TokenType::Number);
		float angle = std::stof(it->value.data());
		++it;

		PARSER_CHECK(it->type == TokenType::Number);
		float x = std::stof(it->value.data());
		++it;

		PARSER_CHECK(it->type == TokenType::Number);
		float y = std::stof(it->value.data());
		++it;

		PARSER_CHECK(it->type == TokenType::Number);
		float z = std::stof(it->value.data());
		++it;

		// #todo-pbrt-parser: Concat with current transform
	}

	void PBRT4ParserEx::concatTransform(TokenIter& it)
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

		// #todo-pbrt-parser: Concat with current transform
	}

}
