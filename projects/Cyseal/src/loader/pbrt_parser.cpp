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
#define DIRECTIVE_TRANSFORM              "Transform"
#define DIRECTIVE_CONCAT_TRANSFORM       "ConcatTransform"
#define DIRECTIVE_AREA_LIGHT_SOURCE      "AreaLightSource"

// Legacy tokens (pbrt-v3)
#define DIRECTIVE_TRANSFORM_BEGIN        "TransformBegin"
#define DIRECTIVE_TRANSFORM_END          "TransformEnd"

// #todo-pbrt-parser: Replace with proper error handling
#define PARSER_CHECK CHECK
#define PARSER_CHECK_NO_ENTRY CHECK_NO_ENTRY

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
		auto it = tokens.begin();
		while (it->type != TokenType::EoF)
		{
			directive(it, output);
		}

		return output;
	}

	void PBRT4Parser::initStates()
	{
		parsePhase = PBRT4ParsePhase::RenderingOptions;
		currentTransform.identity();
		currentTransformBackup.identity();
		bCurrentTransformIsIdentity = true;
		currentNamedMaterial = "";
		currentEmission = vec3(0.0f);
	}

	void PBRT4Parser::directive(TokenIter& it, PBRT4ParserOutput& output)
	{
		if (it->type == TokenType::String)
		{
			// Process some meta directives here, but use function table for others.
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
				currentTransformBackup = currentTransform;
				++it;
			}
			else if (it->value == DIRECTIVE_TRANSFORM_END)
			{
				PARSER_CHECK(parsePhase == PBRT4ParsePhase::InsideAttribute);
				parsePhase = PBRT4ParsePhase::SceneElements;
				currentTransform = currentTransformBackup;
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
				currentEmission = vec3(0.0f);
				++it;
			}
			else
			{
				auto callbackIt = directiveTable.find(std::string(it->value));
				if (callbackIt != directiveTable.end())
				{
					++it;
					callbackIt->second(it, output);
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

	void PBRT4Parser::transform(TokenIter& it, PBRT4ParserOutput& output)
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
			output.sceneTransform = mat;
		}
		else if (parsePhase == PBRT4ParsePhase::InsideAttribute)
		{
			currentTransform = mat;
			bCurrentTransformIsIdentity = false;
		}
		else
		{
			PARSER_CHECK_NO_ENTRY();
		}
	}

	void PBRT4Parser::sampler(TokenIter& it, PBRT4ParserOutput& output)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		const std::string samplerName(it->value);
		
		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed sampler
	}

	void PBRT4Parser::pixelFilter(TokenIter& it, PBRT4ParserOutput& output)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		const std::string pixelFilterName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed pixelFilter
	}

	void PBRT4Parser::film(TokenIter& it, PBRT4ParserOutput& output)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		const std::string filmName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed film
	}

	void PBRT4Parser::camera(TokenIter& it, PBRT4ParserOutput& output)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		// "orthographic", "perspective", "realistic", "spherical"
		const std::string cameraType(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed camera
	}

	void PBRT4Parser::texture(TokenIter& it, PBRT4ParserOutput& output)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);
		std::string textureName(it->value);

		++it;
		PARSER_CHECK(it->type == TokenType::QuoteString);
		std::string textureType(it->value);

		++it;
		PARSER_CHECK(it->type == TokenType::QuoteString);
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

	void PBRT4Parser::makeNamedMaterial(TokenIter& it, PBRT4ParserOutput& output)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);
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
		PARSER_CHECK(parsePhase != PBRT4ParsePhase::RenderingOptions);

		// "bilinearmesh", "curve", "cylinder", "disk", "sphere", "trianglemesh",
		// "loopsubdiv", "plymesh"
		PARSER_CHECK(it->type == TokenType::QuoteString);
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

	void PBRT4Parser::namedMaterial(TokenIter& it, PBRT4ParserOutput& output)
	{
		PARSER_CHECK(it->type == TokenType::QuoteString);

		const std::string materialName(it->value);
		++it;

		currentNamedMaterial = materialName;
	}

	void PBRT4Parser::lightSource(TokenIter& it, PBRT4ParserOutput& output)
	{
		// "distant", "goniometric", "infinite", "point", "projection", "spot"
		PARSER_CHECK(it->type == TokenType::QuoteString);

		const std::string lightName(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed LightSource
	}

	void PBRT4Parser::rotate(TokenIter& it, PBRT4ParserOutput& output)
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

	void PBRT4Parser::concatTransform(TokenIter& it, PBRT4ParserOutput& output)
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

	void PBRT4Parser::areaLightSource(TokenIter& it, PBRT4ParserOutput& output)
	{
		// "diffuse"
		PARSER_CHECK(it->type == TokenType::QuoteString);
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

	void PBRT4Parser::material(TokenIter& it, PBRT4ParserOutput& output)
	{
		// "coateddiffuse", "coatedconductor", "conductor", "dielectric", "diffuse", "diffusetransmission",
		// "hair", "interface", "measured", "mix", "subsurface", "thindieletric"
		PARSER_CHECK(it->type == TokenType::QuoteString);
		const std::string materialType(it->value);

		++it;
		auto params = parameters(it);
		// #todo-pbrt-parser: Emit parsed Material
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
				PARSER_CHECK(it->type == TokenType::Number);
				float value = std::stof(it->value.data());

				PBRT4Parameter param{};
				param.datatype = PBRT4ParameterType::Float;
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
		auto pEta            = findParameter(inDesc.parameters, "eta");
		auto pK              = findParameter(inDesc.parameters, "k");
		COMPILER_CHECK_PARAMETER(pType, PBRT4ParameterType::String);
		COMPILER_OPTIONAL_PARAMETER2(pReflectrance, PBRT4ParameterType::Texture, PBRT4ParameterType::Float3);
		COMPILER_OPTIONAL_PARAMETER(pRemaproughness, PBRT4ParameterType::Bool);
		COMPILER_OPTIONAL_PARAMETER(pRoughness, PBRT4ParameterType::Float);
		COMPILER_OPTIONAL_PARAMETER(pVRoughness, PBRT4ParameterType::Float);
		COMPILER_OPTIONAL_PARAMETER(pURoughness, PBRT4ParameterType::Float);
		COMPILER_OPTIONAL_PARAMETER3(pEta, PBRT4ParameterType::Spectrum, PBRT4ParameterType::Float, PBRT4ParameterType::Float3);
		COMPILER_OPTIONAL_PARAMETER2(pK, PBRT4ParameterType::Spectrum, PBRT4ParameterType::Float3);

		bool bUseRgbReflectance = pReflectrance != nullptr && pReflectrance->datatype == PBRT4ParameterType::Float3;
		bool bUseAnisotrophicRoughness = pVRoughness != nullptr && pURoughness != nullptr;
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
