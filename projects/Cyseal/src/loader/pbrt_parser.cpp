#include "pbrt_parser.h"
#include "core/assertion.h"
#include "render/material.h"
#include "util/string_conversion.h"

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
#define DIRECTIVE_SCALE                  "Scale"
#define DIRECTIVE_TRANSFORM              "Transform"
#define DIRECTIVE_CONCAT_TRANSFORM       "ConcatTransform"
#define DIRECTIVE_AREA_LIGHT_SOURCE      "AreaLightSource"

// Legacy tokens (pbrt-v3)
#define DIRECTIVE_TRANSFORM_BEGIN        "TransformBegin"
#define DIRECTIVE_TRANSFORM_END          "TransformEnd"

// #todo-pbrt-parser: Replace with proper error handling
#define PARSER_CHECK CHECK

// #todo-pbrt-parser: Replace with proper error handling
#define COMPILER_CHECK CHECK
#define COMPILER_CHECK_NO_ENTRY CHECK_NO_ENTRY
#define COMPILER_CHECK_PARAMETER(param, type) COMPILER_CHECK(param != nullptr && param->datatype == type)
#define COMPILER_OPTIONAL_PARAMETER(param, type) COMPILER_CHECK(param == nullptr || param->datatype == type)
#define COMPILER_CHECK_PARAMETER2(param, type1, type2) COMPILER_CHECK(param != nullptr && (param->datatype == type1 || param->datatype == type2))
#define COMPILER_OPTIONAL_PARAMETER2(param, type1, type2) COMPILER_CHECK(param == nullptr || (param->datatype == type1 || param->datatype == type2))
#define COMPILER_OPTIONAL_PARAMETER3(param, type1, type2, type3) COMPILER_CHECK(param == nullptr || (param->datatype == type1 || param->datatype == type2 || param->datatype == type3))

namespace pbrt
{
	std::vector<vec3> toFloat3Array(const std::vector<float>&& inArray)
	{
		std::vector<vec3> outArray;
		outArray.reserve(inArray.size() / 3);
		for (size_t i = 0; i < inArray.size(); i += 3)
		{
			outArray.emplace_back(vec3(inArray[i], inArray[i + 1], inArray[i + 2]));
		}
		return outArray;
	}
	std::vector<vec2> toFloat2Array(const std::vector<float>&& inArray)
	{
		std::vector<vec2> outArray;
		outArray.reserve(inArray.size() / 2);
		for (size_t i = 0; i < inArray.size(); i += 2)
		{
			outArray.emplace_back(vec2(inArray[i], inArray[i + 1]));
		}
		return outArray;
	}
	std::vector<uint32> toUIntArray(const std::vector<int32>&& inArray)
	{
		std::vector<uint32> outArray;
		outArray.reserve(inArray.size());
		for (size_t i = 0; i < inArray.size(); ++i)
		{
			outArray.emplace_back((uint32)inArray[i]);
		}
		return outArray;
	}
}

namespace pbrt
{
	PBRT4Parser::PBRT4Parser()
	{
		directiveTable = {
			{DIRECTIVE_WORLD_BEGIN,         std::bind(&PBRT4Parser::worldBegin,        this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_TRANSFORM_BEGIN,     std::bind(&PBRT4Parser::transformBegin,    this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_TRANSFORM_END,       std::bind(&PBRT4Parser::transformEnd,      this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_ATTRIBUTE_BEGIN,     std::bind(&PBRT4Parser::attributeBegin,    this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_ATTRIBUTE_END,       std::bind(&PBRT4Parser::attributeEnd,      this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_INTEGRATOR,          std::bind(&PBRT4Parser::integrator,        this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_TRANSFORM,           std::bind(&PBRT4Parser::transform,         this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_SAMPLER,             std::bind(&PBRT4Parser::sampler,           this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_PIXEL_FILTER,        std::bind(&PBRT4Parser::pixelFilter,       this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_FILM,                std::bind(&PBRT4Parser::film,              this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_CAMERA,              std::bind(&PBRT4Parser::camera,            this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_TEXTURE,             std::bind(&PBRT4Parser::texture,           this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_MAKE_NAMED_MATERIAL, std::bind(&PBRT4Parser::makeNamedMaterial, this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_SHAPE,               std::bind(&PBRT4Parser::shape,             this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_NAMED_MATERIAL,      std::bind(&PBRT4Parser::namedMaterial,     this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_LIGHT_SOURCE,        std::bind(&PBRT4Parser::lightSource,       this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_ROTATE,              std::bind(&PBRT4Parser::rotate,            this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_SCALE,               std::bind(&PBRT4Parser::scale,             this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_LOOKAT,              std::bind(&PBRT4Parser::lookAt,            this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_CONCAT_TRANSFORM,    std::bind(&PBRT4Parser::concatTransform,   this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_AREA_LIGHT_SOURCE,   std::bind(&PBRT4Parser::areaLightSource,   this, std::placeholders::_1, std::placeholders::_2)},
			{DIRECTIVE_MATERIAL,            std::bind(&PBRT4Parser::material,          this, std::placeholders::_1, std::placeholders::_2)},
		};
	}

	PBRT4ParserOutput PBRT4Parser::parse(PBRT4Scanner* scanner)
	{
		initStates();

		PBRT4ParserOutput output{};

		const std::vector<Token>& tokens = scanner->getTokens();
		if (tokens.size() > 0)
		{
			eofTokenIt = tokens.end() - 1;
			CHECK(eofTokenIt->type == TokenType::EoF);

			auto it = tokens.begin();
			while (it->type != TokenType::EoF)
			{
				directive(it, output);
			}

			output.bValid = bValid;
			output.errorMessages = std::move(errorMessages);
		}

		return output;
	}

	void PBRT4Parser::initStates()
	{
		bValid = true;
		errorMessages.clear();
		parsePhase = PBRT4ParsePhase::RenderingOptions;
		currentTransform.identity();
		currentTransformBackup.identity();
		bCurrentTransformIsIdentity = true;
		currentNamedMaterial = "";
		currentEmission = vec3(0.0f);
	}

	void PBRT4Parser::parserError(TokenIter& it, const wchar_t* msg, ...)
	{
		wchar_t fmtBuffer[4096];
		va_list argptr;
		va_start(argptr, msg);
		std::vswprintf(fmtBuffer, 4096, msg, argptr);
		va_end(argptr);

		errorMessages.emplace_back(fmtBuffer);

		// #todo-pbrt-parser: Stop immediately or synchronize to next directive?
		it = eofTokenIt;
		bValid = false;
	}

	bool PBRT4Parser::parserWrongToken(TokenIter& it, TokenType tokType)
	{
		if (it->type != tokType)
		{
			std::wstring type1 = getTokenTypeWString(tokType);
			std::wstring type2 = getTokenTypeWString(tokType);
			parserError(it, L"Expected: %s, actual: %s", type1.data(), type2.data());
			return true;
		}
		return false;
	}

	void PBRT4Parser::directive(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::String)) return;

		auto callbackIt = directiveTable.find(std::string(it->value));
		if (callbackIt != directiveTable.end())
		{
			++it;
			callbackIt->second(it, output);
		}
		else
		{
			std::string dirName{ it->value };
			parserError(it, L"Unsupported directive: %S", dirName.data());
			return;
		}
	}

	void PBRT4Parser::worldBegin(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parsePhase != PBRT4ParsePhase::RenderingOptions)
		{
			parserError(it, L"WorldBegin directive in wrong place");
			return;
		}

		parsePhase = PBRT4ParsePhase::SceneElements;

		output.sceneTransform = currentTransform;

		currentTransform.identity();
		currentTransformBackup.identity();
	}

	void PBRT4Parser::transformBegin(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parsePhase != PBRT4ParsePhase::SceneElements)
		{
			parserError(it, L"TransformBegin directive in wrong place");
			return;
		}

		parsePhase = PBRT4ParsePhase::InsideAttribute;
		currentTransformBackup = currentTransform;
	}

	void PBRT4Parser::transformEnd(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parsePhase != PBRT4ParsePhase::InsideAttribute)
		{
			parserError(it, L"TransformEnd directive in wrong place");
			return;
		}

		parsePhase = PBRT4ParsePhase::SceneElements;
		currentTransform = currentTransformBackup;
	}

	void PBRT4Parser::attributeBegin(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parsePhase != PBRT4ParsePhase::SceneElements)
		{
			parserError(it, L"AttributeBegin directive in wrong place");
			return;
		}

		parsePhase = PBRT4ParsePhase::InsideAttribute;
		currentTransformBackup = currentTransform;
	}

	void PBRT4Parser::attributeEnd(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parsePhase != PBRT4ParsePhase::InsideAttribute)
		{
			parserError(it, L"AttributeEnd directive in wrong place");
			return;
		}

		parsePhase = PBRT4ParsePhase::SceneElements;
		currentTransform = currentTransformBackup;
		currentEmission = vec3(0.0f);
	}

	void PBRT4Parser::integrator(TokenIter& it, PBRT4ParserOutput& output)
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

		if (parserWrongToken(it, TokenType::QuoteString)) return;

		Integrator integratorDesc{};

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
			std::string invalidName{ it->value };
			parserError(it, L"Invalid integrator name: %S", invalidName.data());
			return;
		}
	}

	void PBRT4Parser::sampler(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::QuoteString)) return;

		const std::string samplerName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed sampler
	}

	void PBRT4Parser::pixelFilter(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::QuoteString)) return;

		const std::string pixelFilterName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed pixelFilter
	}

	void PBRT4Parser::film(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::QuoteString)) return;

		const std::string filmName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed film
	}

	void PBRT4Parser::camera(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::QuoteString)) return;

		// "orthographic", "perspective", "realistic", "spherical"
		const std::string cameraType(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed camera
	}

	void PBRT4Parser::transform(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::LeftBracket)) return;
		++it;

		Matrix mat;
		for (size_t i = 0; i < 16; ++i)
		{
			if (parserWrongToken(it, TokenType::Number)) return;

			float value = std::stof(it->value.data());
			mat.m[i / 4][i % 4] = value;
			++it;
		}

		if (parserWrongToken(it, TokenType::RightBracket)) return;
		++it;

		if (parsePhase == PBRT4ParsePhase::SceneElements)
		{
			parserError(it, L"Transform directive in wrong place");
			return;
		}
		currentTransform = mat;
		bCurrentTransformIsIdentity = false;
	}

	void PBRT4Parser::rotate(TokenIter& it, PBRT4ParserOutput& output)
	{
		// Rotate angle x y z
		// where angle is in degrees and (x, y, z) = axis

		if (parserWrongToken(it, TokenType::Number)) return;
		float angle = std::stof(it->value.data());
		++it;

		if (parserWrongToken(it, TokenType::Number)) return;
		float x = std::stof(it->value.data());
		++it;

		if (parserWrongToken(it, TokenType::Number)) return;
		float y = std::stof(it->value.data());
		++it;

		if (parserWrongToken(it, TokenType::Number)) return;
		float z = std::stof(it->value.data());
		++it;

		// #todo-pbrt-parser: Concat with current transform
	}

	void PBRT4Parser::scale(TokenIter& it, PBRT4ParserOutput& output)
	{
		// Scale x y z

		if (parserWrongToken(it, TokenType::Number)) return;
		float x = std::stof(it->value.data());
		++it;

		if (parserWrongToken(it, TokenType::Number)) return;
		float y = std::stof(it->value.data());
		++it;

		if (parserWrongToken(it, TokenType::Number)) return;
		float z = std::stof(it->value.data());
		++it;

		Matrix S;
		S.scale(x, y, z);

		currentTransform = S * currentTransform;
	}

	void PBRT4Parser::lookAt(TokenIter& it, PBRT4ParserOutput& output)
	{
		// LookAt eye_x eye_y eye_z look_x look_y look_z up_x up_y up_z

		auto readFloat = [&it, this](float& outValue) {
			if (parserWrongToken(it, TokenType::Number)) return true;
			outValue = std::stof(it->value.data());
			++it;
			return false;
		};

		vec3 origin, target, up;
		if (readFloat(origin.x)) return;
		if (readFloat(origin.y)) return;
		if (readFloat(origin.z)) return;
		if (readFloat(target.x)) return;
		if (readFloat(target.y)) return;
		if (readFloat(target.z)) return;
		if (readFloat(up.x)) return;
		if (readFloat(up.y)) return;
		if (readFloat(up.z)) return;

		//~ Burrowed from Camera::lookAt()
		vec3 Z = normalize(target - origin); // forward
		vec3 X = normalize(cross(Z, up));    // right
		vec3 Y = cross(X, Z);                // up
		float M[16] = {
			X.x,             Y.x,            -Z.x,             0.0f,
			X.y,             Y.y,            -Z.y,             0.0f,
			X.z,             Y.z,            -Z.z,             0.0f,
			-dot(X, origin), -dot(Y, origin), dot(Z, origin),  1.0f
		};
		//~

		currentTransform.copyFrom(M);
	}

	void PBRT4Parser::concatTransform(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::LeftBracket)) return;
		++it;

		Matrix M;
		for (size_t i = 0; i < 16; ++i)
		{
			if (parserWrongToken(it, TokenType::Number)) return;
			float value = std::stof(it->value.data());
			M.m[i / 4][i % 4] = value;
			++it;
		}
		if (parserWrongToken(it, TokenType::RightBracket)) return;
		++it;

		currentTransform = M * currentTransform;
	}

	void PBRT4Parser::texture(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::QuoteString)) return;
		std::string textureName(it->value);

		++it;
		if (parserWrongToken(it, TokenType::QuoteString)) return;
		std::string textureType(it->value);

		++it;
		if (parserWrongToken(it, TokenType::QuoteString)) return;
		std::string textureClass(it->value);

		++it;
		auto params = parameters(it);

		TextureDesc desc{
			.name         = std::move(textureName),
			.textureType  = std::move(textureType),
			.textureClass = std::move(textureClass),
			.parameters   = std::move(params),
		};
		compileTexture(desc, output);
	}

	void PBRT4Parser::material(TokenIter& it, PBRT4ParserOutput& output)
	{
		// "coateddiffuse", "coatedconductor", "conductor", "dielectric", "diffuse", "diffusetransmission",
		// "hair", "interface", "measured", "mix", "subsurface", "thindieletric"
		if (parserWrongToken(it, TokenType::QuoteString)) return;
		const std::string materialType(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed Material
	}

	void PBRT4Parser::namedMaterial(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::QuoteString)) return;

		const std::string materialName(it->value);
		++it;

		currentNamedMaterial = materialName;
	}

	void PBRT4Parser::makeNamedMaterial(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parserWrongToken(it, TokenType::QuoteString)) return;
		std::string materialName(it->value);

		++it;
		auto params = parameters(it);

		MaterialDesc materialDesc{
			.name       = std::move(materialName),
			.parameters = std::move(params),
		};
		compileMaterial(materialDesc, output);
	}

	void PBRT4Parser::shape(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (parsePhase == PBRT4ParsePhase::RenderingOptions)
		{
			parserError(it, L"Shape directive in wrong place");
			return;
		}

		// "bilinearmesh", "curve", "cylinder", "disk", "sphere", "trianglemesh",
		// "loopsubdiv", "plymesh"
		if (parserWrongToken(it, TokenType::QuoteString)) return;
		std::string shapeName(it->value);

		++it;
		auto params = parameters(it);

		ShapeDesc shapeDesc{
			.name               = std::move(shapeName),
			.namedMaterial      = currentNamedMaterial,
			.transform          = currentTransform,
			.bIdentityTransform = bCurrentTransformIsIdentity,
			.parameters         = std::move(params),
		};
		compileShape(shapeDesc, output);
	}

	void PBRT4Parser::lightSource(TokenIter& it, PBRT4ParserOutput& output)
	{
		// "distant", "goniometric", "infinite", "point", "projection", "spot"
		if (parserWrongToken(it, TokenType::QuoteString)) return;

		const std::string lightName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed LightSource
	}

	void PBRT4Parser::areaLightSource(TokenIter& it, PBRT4ParserOutput& output)
	{
		// "diffuse"
		if (parserWrongToken(it, TokenType::QuoteString)) return;
		const std::string lightType(it->value);

		++it;
		auto params = parameters(it);
		
		if (lightType == "diffuse")
		{
			auto pL = findParameter(params, "L");
			COMPILER_CHECK_PARAMETER(pL, PBRT4ParameterType::Float3);

			currentEmission = pL->asFloat3;
		}
		else
		{
			COMPILER_CHECK_NO_ENTRY();
		}
	}

	std::vector<PBRT4Parameter> PBRT4Parser::parameters(TokenIter& it)
	{
		std::vector<PBRT4Parameter> params;

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

					PBRT4Parameter param{};
					param.datatype = PBRT4ParameterType::Int;
					param.name = pname;
					param.asInt = values[0];
					params.emplace_back(param);
				}
				else
				{
					--it;

					PBRT4Parameter param{};
					param.datatype = PBRT4ParameterType::IntArray;
					param.name = pname;
					param.asIntArray = std::move(values);
					params.emplace_back(param);
				}
			}
			else if (ptype == "float")
			{
				std::vector<float> values;
				while (it->type == TokenType::Number)
				{
					float value = std::stof(it->value.data());
					values.emplace_back(value);
					++it;
				}
				PARSER_CHECK(values.size() > 0);
				PARSER_CHECK(values.size() == 1 || hasBrackets);

				if (values.size() == 1)
				{
					if (hasBrackets) --it;

					PBRT4Parameter param{};
					param.datatype = PBRT4ParameterType::Float;
					param.name = pname;
					param.asFloat = values[0];
					params.emplace_back(param);
				}
				else
				{
					--it;

					PBRT4Parameter param{};
					param.datatype = PBRT4ParameterType::FloatArray;
					param.name = pname;
					param.asFloatArray = std::move(values);
					params.emplace_back(param);
				}
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

				PBRT4Parameter param{};
				param.datatype = PBRT4ParameterType::Float3;
				param.name = pname;
				param.asFloat3 = vec3(R, G, B);
				params.emplace_back(param);
			}
			else if (ptype == "string")
			{
				PARSER_CHECK(it->type == TokenType::QuoteString);
				std::string strValue(it->value);

				PBRT4Parameter param{};
				param.datatype = PBRT4ParameterType::String;
				param.name = pname;
				param.asString = std::move(strValue);
				params.emplace_back(param);
			}
			else if (ptype == "bool")
			{
				PARSER_CHECK(it->type == TokenType::String);
				std::string str(it->value);
				PARSER_CHECK(str == "true" || str == "false");

				PBRT4Parameter param{};
				param.datatype = PBRT4ParameterType::Bool;
				param.name = pname;
				param.asBool = (str == "true");
				params.emplace_back(param);
			}
			else if (ptype == "texture")
			{
				PARSER_CHECK(it->type == TokenType::QuoteString);
				std::string textureName(it->value);

				PBRT4Parameter param{};
				param.datatype = PBRT4ParameterType::Texture;
				param.name = pname;
				param.asString = textureName;
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

				PBRT4Parameter param{};
				param.datatype = PBRT4ParameterType::Float2Array;
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

				PBRT4Parameter param{};
				param.datatype = PBRT4ParameterType::Float3Array;
				param.name = pname;
				param.asFloatArray = std::move(float3Array);
				params.emplace_back(param);
			}
			else if (ptype == "spectrum")
			{
				PARSER_CHECK(it->type == TokenType::QuoteString);
				std::string spectrumName(it->value);

				PBRT4Parameter param{};
				param.datatype = PBRT4ParameterType::Spectrum;
				param.name = pname;
				param.asString = spectrumName;
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

	pbrt::PBRT4Parameter* PBRT4Parser::findParameter(ParameterList& params, const char* pname) const
	{
		for (auto& param : params)
		{
			if (param.name == pname) return &param;
		}
		return nullptr;
	}

	void PBRT4Parser::compileShape(ShapeDesc& inDesc, PBRT4ParserOutput& output)
	{
		if (inDesc.name == "plymesh")
		{
			const auto pFilename = findParameter(inDesc.parameters, "filename");
			COMPILER_CHECK_PARAMETER(pFilename, PBRT4ParameterType::String);

			std::string plyFilename = pFilename->asString;
			std::wstring wPlyFilename;
			str_to_wstr(plyFilename, wPlyFilename);

			PBRT4ParserOutput::PLYShapeDesc outDesc{
				.filename           = wPlyFilename,
				.namedMaterial      = inDesc.namedMaterial,
				.transform          = inDesc.transform,
				.bIdentityTransform = inDesc.bIdentityTransform,
			};
			output.plyShapeDescs.emplace_back(outDesc);
		}
		else if (inDesc.name == "trianglemesh")
		{
			auto pTexcoords = findParameter(inDesc.parameters, "uv");
			auto pNormals   = findParameter(inDesc.parameters, "N");
			auto pPositions = findParameter(inDesc.parameters, "P");
			auto pIndices   = findParameter(inDesc.parameters, "indices");
			COMPILER_CHECK_PARAMETER(pTexcoords, PBRT4ParameterType::Float2Array);
			COMPILER_CHECK_PARAMETER(pNormals, PBRT4ParameterType::Float3Array);
			COMPILER_CHECK_PARAMETER(pPositions, PBRT4ParameterType::Float3Array);
			COMPILER_CHECK_PARAMETER(pIndices, PBRT4ParameterType::IntArray);

			SharedPtr<MaterialAsset> material = makeShared<MaterialAsset>();
			material->emission = currentEmission;

			PBRT4ParserOutput::TriangleMeshDesc outDesc{
				.positionBuffer = toFloat3Array(std::move(pPositions->asFloatArray)),
				.normalBuffer   = toFloat3Array(std::move(pNormals->asFloatArray)),
				.texcoordBuffer = toFloat2Array(std::move(pTexcoords->asFloatArray)),
				.indexBuffer    = toUIntArray(std::move(pIndices->asIntArray)),
				.material       = material,
			};
			output.triangleShapeDescs.emplace_back(outDesc);
		}
	}

	void PBRT4Parser::compileMaterial(MaterialDesc& inDesc, PBRT4ParserOutput& output)
	{
		auto pType           = findParameter(inDesc.parameters, "type");
		auto pReflectrance   = findParameter(inDesc.parameters, "reflectance");
		auto pRemaproughness = findParameter(inDesc.parameters, "remaproughness");
		auto pRoughness      = findParameter(inDesc.parameters, "roughness");
		auto pVRoughness     = findParameter(inDesc.parameters, "vroughness");
		auto pURoughness     = findParameter(inDesc.parameters, "uroughness");
		auto pTransmittance  = findParameter(inDesc.parameters, "transmittance");
		auto pEta            = findParameter(inDesc.parameters, "eta");
		auto pK              = findParameter(inDesc.parameters, "k");
		COMPILER_CHECK_PARAMETER(pType, PBRT4ParameterType::String);
		COMPILER_OPTIONAL_PARAMETER2(pReflectrance, PBRT4ParameterType::Texture, PBRT4ParameterType::Float3);
		COMPILER_OPTIONAL_PARAMETER(pRemaproughness, PBRT4ParameterType::Bool);
		COMPILER_OPTIONAL_PARAMETER(pRoughness, PBRT4ParameterType::Float);
		COMPILER_OPTIONAL_PARAMETER(pVRoughness, PBRT4ParameterType::Float);
		COMPILER_OPTIONAL_PARAMETER(pURoughness, PBRT4ParameterType::Float);
		COMPILER_OPTIONAL_PARAMETER(pTransmittance, PBRT4ParameterType::Float3);
		COMPILER_OPTIONAL_PARAMETER3(pEta, PBRT4ParameterType::Spectrum, PBRT4ParameterType::Float, PBRT4ParameterType::Float3);
		COMPILER_OPTIONAL_PARAMETER2(pK, PBRT4ParameterType::Spectrum, PBRT4ParameterType::Float3);

		bool bUseRgbReflectance = pReflectrance != nullptr && pReflectrance->datatype == PBRT4ParameterType::Float3;
		bool bUseAnisotrophicRoughness = pVRoughness != nullptr && pURoughness != nullptr;
		bool bTransmissive = pTransmittance != nullptr && allLessThan(pTransmittance->asFloat3, vec3(1.0f));
		bool bUseRgbEtaAndK = pEta != nullptr && pK != nullptr && pEta->datatype != PBRT4ParameterType::Spectrum && pK->datatype != PBRT4ParameterType::Spectrum;

		vec3 rgbReflectance(1.0f); std::string textureReflectance;
		if (pReflectrance != nullptr)
		{
			if (bUseRgbReflectance) rgbReflectance = pReflectrance->asFloat3;
			else textureReflectance = pReflectrance->asString;
		}

		float roughness = 1.0f, vroughness = 1.0f, uroughness = 1.0f;
		if (bUseAnisotrophicRoughness)
		{
			vroughness = pVRoughness->asFloat;
			uroughness = pURoughness->asFloat;
		}
		else
		{
			roughness = pRoughness != nullptr ? pRoughness->asFloat : 1.0f;
		}

		vec3 transmittance(0.0f);
		if (bTransmissive)
		{
			transmittance = pTransmittance->asFloat3;
		}

		vec3 rgbEta(0.0f), rgbK(0.0f);
		if (bUseRgbEtaAndK && pEta != nullptr && pK != nullptr)
		{
			rgbEta = (pEta->datatype == PBRT4ParameterType::Float) ? vec3(pEta->asFloat) : pEta->asFloat3;
			rgbK = (pK->datatype == PBRT4ParameterType::Float) ? vec3(pK->asFloat) : pK->asFloat3;
		}

		PBRT4ParserOutput::NamedMaterialDesc outDesc{
			.materialName             = std::move(inDesc.name),
			.materialType             = std::move(pType->asString),
			.bUseRgbReflectance       = bUseRgbReflectance,
			.rgbReflectance           = rgbReflectance,
			.textureReflectance       = textureReflectance,
			.bUseAnisotropicRoughness = bUseAnisotrophicRoughness,
			.bRemapRoughness          = (pRemaproughness != nullptr) ? pRemaproughness->asBool : false,
			.roughness                = roughness,
			.vroughness               = vroughness,
			.uroughness               = uroughness,
			.bTransmissive            = bTransmissive,
			.transmittance            = transmittance,
			.bUseRgbEtaAndK           = bUseRgbEtaAndK,
			.rgbEta                   = rgbEta,
			.rgbK                     = rgbK,
			.spectrumEta              = (bUseRgbEtaAndK || pEta == nullptr) ? "" : std::move(pEta->asString),
			.spectrumK                = (bUseRgbEtaAndK || pK == nullptr) ? "" : std::move(pK->asString),
		};
		output.namedMaterialDescs.emplace_back(outDesc);
	}

	void PBRT4Parser::compileTexture(TextureDesc& inDesc, PBRT4ParserOutput& output)
	{
		if (inDesc.textureType == "spectrum" && inDesc.textureClass == "imagemap")
		{
			auto pFilter   = findParameter(inDesc.parameters, "filter");
			auto pFilename = findParameter(inDesc.parameters, "filename");
			COMPILER_CHECK_PARAMETER(pFilter, PBRT4ParameterType::String);
			COMPILER_CHECK_PARAMETER(pFilename, PBRT4ParameterType::String);

			std::wstring wTextureFilename;
			str_to_wstr(pFilename->asString, wTextureFilename);

			PBRT4ParserOutput::TextureFileDesc outDesc{
				.textureName   = std::move(inDesc.name),
				.textureFilter = std::move(pFilename->asString),
				.filename      = std::move(wTextureFilename),
			};
			output.textureFileDescs.emplace_back(outDesc);
		}
		else
		{
			COMPILER_CHECK_NO_ENTRY();
		}
	}

}
