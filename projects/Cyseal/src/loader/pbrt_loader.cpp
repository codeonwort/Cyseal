#include "pbrt_loader.h"
#include "ply_loader.h"
#include "image_loader.h"

#include "core/assertion.h"
#include "core/smart_pointer.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/gpu_resource.h"
#include "rhi/texture_manager.h"
#include "render/material.h"
#include "world/gpu_resource_asset.h"
#include "util/resource_finder.h"
#include "util/string_conversion.h"
#include "util/logging.h"

#include <vector>
#include <fstream>
#include <filesystem>

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

// Legacy tokens (pbrt-v3)
#define TOKEN_TRANSFORM_BEGIN        "TransformBegin"
#define TOKEN_TRANSFORM_END          "TransformEnd"

DEFINE_LOG_CATEGORY_STATIC(LogPBRT);

// PBRT4Parser utils
namespace
{
	enum class PBRT4ParsePhase
	{
		RenderingOptions = 0,
		SceneElements    = 1,
		InsideAttribute  = 2,
	};

	struct TextureFileDesc
	{
		std::string textureName;
		std::string textureFilter; // #todo-pbrt: textureFilter
		std::wstring filename;
	};

	struct NamedMaterialDesc
	{
		std::string materialName;
		std::string materialType;

		bool bUseRgbReflectance = false;
		vec3 rgbReflectance = vec3(1.0f);
		std::string textureReflectance;

		bool bUseAnisotropicRoughness = false;
		bool bRemapRoughness = false;
		float roughness = 1.0f;
		float vroughness = 1.0f;
		float uroughness = 1.0f;

		bool bUseRgbEtaAndK = false;
		vec3 rgbEta, rgbK;
		std::string spectrumEta, spectrumK;
	};

	struct PLYShapeDesc
	{
		std::wstring filename;
		std::string  namedMaterial;
		Matrix transform;
		bool bIdentityTransform;
	};

	std::istream& operator>>(std::istream& stream, vec3& v)
	{
		stream >> v.x >> v.y >> v.z;
		return stream;
	}

	// \"asd\" -> std::string("asd")
	std::string readQuoteWord(std::istream& stream)
	{
		std::string str;
		stream >> str;
		return str.substr(1, str.size() - 2);
	} 
	// \"asd zxc\" -> std::string("asd zxc")
	std::string readQuoteTwoWords(std::istream& stream)
	{
		std::string word1, word2;
		stream >> word1 >> word2;
		return word1.substr(1) + " " + word2.substr(0, word2.size() - 1);
	}
	std::pair<std::string, std::string> readQuoteTwoWordsVer2(std::istream& stream)
	{
		std::string word1, word2;
		stream >> word1 >> word2;
		return std::make_pair(word1.substr(1), word2.substr(0, word2.size() - 1));
	}
	// [ "asd" ] -> std::string("asd")
	std::string readBracketQuoteWord(std::istream& stream)
	{
		char ch;
		stream >> ch;
		std::string str = readQuoteWord(stream);
		stream >> ch;
		return str;
	}
	// [ false ] -> false
	bool readBracketBool(std::istream& stream)
	{
		char ch;
		bool x;
		stream >> ch >> x >> ch;
		return x;
	}
	// [ 1.0 ] -> 1.0
	float readBracketFloat(std::istream& stream)
	{
		char ch;
		float x;
		stream >> ch >> x >> ch;
		return x;
	}
	// [ 1 ] -> 1
	int32 readBracketInt32(std::istream& stream)
	{
		char ch;
		int32 x;
		stream >> ch >> x >> ch;
		return x;
	}
	// [ 1 2 3 ] -> vec3(1, 2, 3)
	vec3 readBracketVec3(std::istream& stream)
	{
		char ch;
		vec3 v;
		stream >> ch >> v.x >> v.y >> v.z >> ch;
		return v;
	}
	// If next input starts with ", parse the words enclosed.
	// Otherwise, parse an ordinary string.
	//     MakeNamedMaterial => MakeNamedMaterial
	//     "string type"     => string type
	std::string readMaybeQuoteWords(std::istream& stream)
	{
		std::string str;
		char ch;
		stream >> ch;
		stream.unsetf(std::ios::skipws);
		if (ch == '"')
		{
			while (true)
			{
				stream >> ch;
				if (ch == '"') break;
				str += ch;
			}
		}
		else
		{
			stream >> str;
			str = ch + str;
		}
		stream.setf(std::ios::skipws);
		return str;
	}
	Matrix readBracketMatrix(std::istream& stream)
	{
		char ch;
		Matrix mat;
		stream >> ch;
		for (uint32 i = 0; i < 16; ++i)
		{
			stream >> mat.m[i / 4][i % 4];
		}
		stream >> ch;
		return mat;
	}
	// There might be some smart way but I'm too lazy
	void readBracketFloatArray(std::istream& stream, std::vector<float>& outArray)
	{
		char ch;
		stream >> ch;

		std::string str;
		while (true)
		{
			stream >> str;
			if (str == "]")
			{
				break;
			}
			std::stringstream ss(str);
			float x;
			ss >> x;
			outArray.push_back(x);
		}
	}
	void readBracketVec2Array(std::istream& stream, std::vector<vec2>& outArray)
	{
		char ch;
		stream >> ch;

		std::string str;
		while (true)
		{
			stream >> str;
			if (str == "]")
			{
				break;
			}
			std::stringstream ss(str);
			vec2 v;
			ss >> v.x;
			stream >> v.y;
			outArray.push_back(v);
		}
	}
	void readBracketVec3Array(std::istream& stream, std::vector<vec3>& outArray)
	{
		char ch;
		stream >> ch;

		std::string str;
		while (true)
		{
			stream >> str;
			if (str == "]")
			{
				break;
			}
			std::stringstream ss(str);
			vec3 v;
			ss >> v.x;
			stream >> v.y >> v.z;
			outArray.push_back(v);
		}
	}
	void readBracketUint32Array(std::istream& stream, std::vector<uint32>& outArray)
	{
		char ch;
		stream >> ch;

		std::string str;
		while (true)
		{
			stream >> str;
			if (str == "]")
			{
				break;
			}
			std::stringstream ss(str);
			uint32 i;
			ss >> i;
			outArray.push_back(i);
		}
	}

	bool peekQuoteString(std::istream& stream)
	{
		auto posBackup = stream.tellg();
		std::string str;
		stream >> str;
		stream.seekg(posBackup);
		return str.size() > 0 && str[0] == '\"';
	}

	enum class PBRT4ParameterType { String, Float, Int, Bool };
	struct PBRT4Parameter
	{
		PBRT4ParameterType datatype;
		std::string name;

		std::string asString;
		float asFloat = 0.0f;
		int32 asInt = 0;
		bool asBool = false;
	};
	std::vector<PBRT4Parameter> readParameters(std::istream& stream)
	{
		// #todo-pbrt: Might be not two words and prefix is not someting ordinary
		// "string type" "diffuse" "rgb reflectance" [ 0.1 0.5 0.2 ]
		std::vector<PBRT4Parameter> paramArray;
		while (peekQuoteString(stream))
		{
			auto typeAndName = readQuoteTwoWordsVer2(stream);
			PBRT4Parameter param{};
			param.name = typeAndName.second;
			if (typeAndName.first == "string")
			{
				param.datatype = PBRT4ParameterType::String;
				param.asString = readBracketQuoteWord(stream);
			}
			else if (typeAndName.first == "float")
			{
				param.datatype = PBRT4ParameterType::Float;
				param.asFloat = readBracketFloat(stream);
			}
			else if (typeAndName.first == "integer")
			{
				param.datatype = PBRT4ParameterType::Int;
				param.asInt = readBracketInt32(stream);
			}
			else if (typeAndName.first == "bool")
			{
				param.datatype = PBRT4ParameterType::Bool;
				param.asBool = readBracketBool(stream);
			}
			else
			{
				CHECK_NO_ENTRY();
			}
			paramArray.emplace_back(param);
		}
		return paramArray;
	}
}

class PBRT4Parser
{
public:
	bool parse(std::istream& stream, PBRT4Scene* pbrtScene)
	{
		// Initialize states.
		initializeStates();

		// Steps:
		// 1. Rendering options
		// 2. WorldBegin
		// 3. Lights, geometries, and volumes
		while (!stream.eof())
		{
			std::string token;
			if (queuedToken.size() > 0)
			{
				token = queuedToken;
				queuedToken = "";
			}
			else
			{
				stream >> token;
			}

			if (token == TOKEN_LOOKAT)
			{
				if (!checkRenderingOptionsPhase()) break;
				parseLookAt(stream, pbrtScene);
			}
			else if (token == TOKEN_CAMERA)
			{
				if (!checkRenderingOptionsPhase()) break;
				parseCamera(stream);
			}
			else if (token == TOKEN_SAMPLER)
			{
				if (!checkRenderingOptionsPhase()) break;
				parseSampler(stream);
			}
			else if (token == TOKEN_INTEGRATOR)
			{
				if (!checkRenderingOptionsPhase()) break;
				parseIntegrator(stream);
			}
			else if (token == TOKEN_PIXEL_FILTER)
			{
				if (!checkRenderingOptionsPhase()) break;
				parsePixelFilter(stream);
			}
			else if (token == TOKEN_FILM)
			{
				if (!checkRenderingOptionsPhase()) break;
				parseFilm(stream);
			}
			else if (token == TOKEN_WORLD_BEGIN)
			{
				if (parsePhase == PBRT4ParsePhase::RenderingOptions)
				{
					parsePhase = PBRT4ParsePhase::SceneElements;
				}
				else
				{
					bValidFormat = false;
					CYLOG(LogPBRT, Error, L"WorldBegin token detected but current phase is not RenderingOptions");
					break;
				}
			}
			else if (token == TOKEN_TEXTURE)
			{
				parseTexture(stream);
			}
			else if (token == TOKEN_MAKE_NAMED_MATERIAL)
			{
				parseMakeNamedMaterial(stream);
			}
			else if (token == TOKEN_NAMED_MATERIAL)
			{
				currentNamedMaterial = readQuoteWord(stream);
			}
			else if (token == TOKEN_SHAPE)
			{
				if (!checkNotRenderingOptionsPhase()) break;
				parseShape(stream);
			}
			else if (token == TOKEN_LIGHT_SOURCE)
			{
				// #todo-pbrt: Parse token LightSource
				int z = 0;
			}
			else if (token == TOKEN_TRANSFORM)
			{
				if (parsePhase == PBRT4ParsePhase::RenderingOptions)
				{
					sceneTransform = readBracketMatrix(stream);
				}
				else if (parsePhase == PBRT4ParsePhase::InsideAttribute)
				{
					currentTransform = readBracketMatrix(stream);
					bCurrentTransformIsIdentity = false;
				}
			}
			else if (token == TOKEN_AREA_LIGHT_SOURCE)
			{
				parseAreaLightSource(stream);
			}
			else if (token == TOKEN_ATTRIBUTE_BEGIN)
			{
				CHECK(parsePhase == PBRT4ParsePhase::SceneElements);
				// #todo-pbrt: Parse token AttributeBegin
				parsePhase = PBRT4ParsePhase::InsideAttribute;
			}
			else if (token == TOKEN_ATTRIBUTE_END)
			{
				CHECK(parsePhase == PBRT4ParsePhase::InsideAttribute);
				// #todo-pbrt: Parse token AttributeEnd
				parsePhase = PBRT4ParsePhase::SceneElements;
				currentTransform.identity();
				bCurrentTransformIsIdentity = true;
				currentEmission = vec3(0.0f);
			}
			else if (token == TOKEN_TRANSFORM_BEGIN)
			{
				CHECK(parsePhase == PBRT4ParsePhase::SceneElements);
				parsePhase = PBRT4ParsePhase::InsideAttribute; // Just reuse InsideAttribute phase
				CYLOG(LogPBRT, Warning, L"TransformBegin is deprecated");
			}
			else if (token == TOKEN_TRANSFORM_END)
			{
				CHECK(parsePhase == PBRT4ParsePhase::InsideAttribute);
				parsePhase = PBRT4ParsePhase::SceneElements;
				currentTransform.identity();
				bCurrentTransformIsIdentity = true;
			}
			else
			{
				CYLOG(LogPBRT, Error, L"Can't parse token: %S", token.c_str());
			}
		}

		return bValidFormat;
	}

private:
	void initializeStates()
	{
		parsePhase = PBRT4ParsePhase::RenderingOptions;
		queuedToken = "";
		currentNamedMaterial = "";
		currentTransform.identity();
		currentEmission = vec3(0.0f);
		bCurrentTransformIsIdentity = true;
		bValidFormat = true;
	}
	void enqueueToken(const std::string& tok)
	{
		CHECK(queuedToken.size() == 0);
		queuedToken = tok;
	}
	bool checkRenderingOptionsPhase()
	{
		if (parsePhase != PBRT4ParsePhase::RenderingOptions)
		{
			bValidFormat = false;
			return false;
		}
		return true;
	}
	bool checkNotRenderingOptionsPhase()
	{
		if (parsePhase == PBRT4ParsePhase::RenderingOptions)
		{
			bValidFormat = false;
			return false;
		}
		return true;
	}

	void parseLookAt(std::istream& stream, PBRT4Scene* pbrtScene)
	{
		stream >> pbrtScene->eyePosition;
		stream >> pbrtScene->lookAtPosition;
		stream >> pbrtScene->upVector;
	}
	void parseCamera(std::istream& stream)
	{
		// "orthographic", "perspective", "realistic", "spherical"
		std::string camera = readQuoteWord(stream);

		// #todo-pbrt: Parse Camera parameters.
		std::vector<PBRT4Parameter> params = readParameters(stream);
	}
	void parseSampler(std::istream& stream)
	{
		// "halton"
		// "independent"
		// "paddedsobol"
		// "sobol"
		// "stratified"
		// "zsobol"
		std::string sampler = readQuoteWord(stream);

		// #todo-pbrt: Parse Sampler parameters.
		std::vector<PBRT4Parameter> params = readParameters(stream);
	}
	void parseIntegrator(std::istream& stream)
	{
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
		std::string integrator = readQuoteWord(stream);

		// #todo-pbrt: Utilize these parameters.
		int32 maxDepth = 5;
		std::string lightsampler = "bvh";
		bool regularize = false;

		const auto parameters = readParameters(stream);
		for (const PBRT4Parameter& param : parameters)
		{
			if (param.name == "maxdepth")
			{
				CHECK(param.datatype == PBRT4ParameterType::Int);
				maxDepth = param.asInt;
				if (integrator == "ambientocclusion")
				{
					CYLOG(LogPBRT, Error, L"maxdepth parameter is invalid for integrator=ambientocclusion");
				}
			}
			else if (param.name == "lightsampler")
			{
				CHECK(param.datatype == PBRT4ParameterType::String);
				lightsampler = param.asString;
				if (integrator != "path" && integrator != "volpath")
				{
					CYLOG(LogPBRT, Error, L"lightsampler parameter is only valid for integrator=path,volpath");
				}
				if (lightsampler != "bvh" && lightsampler != "uniform" && lightsampler != "power")
				{
					CYLOG(LogPBRT, Error, L"lightsampler parameter should be \"bvh\", \"uniform\", or \"power\"");
				}
			}
			else if (param.name == "regularize")
			{
				CHECK(param.datatype == PBRT4ParameterType::Bool);
				regularize = param.asBool;
				if (integrator != "bdpt" && integrator != "mlt" && integrator != "path" && integrator != "volpath")
				{
					CYLOG(LogPBRT, Error, L"regularize parameter is only valid for integrator=bdpt,mlt,path,volpath");
				}
			}
		}

		// #todo-pbrt: Parse more parameters for each integrator
		// e.g., cossample for ambientocclusion integrator.
	}
	void parsePixelFilter(std::istream& stream)
	{
		// "box", "gaussian", "mitchell", "sinc", "triangle"
		std::string pixelFilter = readQuoteWord(stream);

		// #todo-pbrt: Parse PixelFilter parameters.
		std::vector<PBRT4Parameter> params = readParameters(stream);
	}
	void parseFilm(std::istream& stream)
	{
		// "rgb", "gbuffer", "spectral"
		std::string film = readQuoteWord(stream);

		// #todo-pbrt: Parse Film parameters.
		std::vector<PBRT4Parameter> params = readParameters(stream);
	}

	void parseTexture(std::istream& stream)
	{
		std::string textureName = readQuoteWord(stream);
		std::string textureType = readQuoteWord(stream);
		if (textureType == "spectrum")
		{
			std::string textureClass = readQuoteWord(stream);
			if (textureClass == "imagemap")
			{
				std::string textureFilter, textureFilename;
				for (uint32 i = 0; i < 2; ++i)
				{
					std::string paramName = readQuoteTwoWords(stream);
					if (paramName == "string filter")
					{
						// #todo-pbrt: texture filter
						textureFilter = readQuoteWord(stream);
					}
					else if (paramName == "string filename")
					{
						textureFilename = readBracketQuoteWord(stream);
					}
				}
				std::wstring wTextureFilename;
				str_to_wstr(textureFilename, wTextureFilename);
				TextureFileDesc desc{
					.textureName = textureName,
					.textureFilter = textureFilter,
					.filename = wTextureFilename
				};
				textureFileDescs.emplace_back(desc);
			}
			else
			{
				bValidFormat = false;
				CYLOG(LogPBRT, Error, L"Unknown texture class: %S", textureClass.c_str());
			}
		}
		else if (textureType == "float")
		{
			// #todo-pbrt
			bValidFormat = false;
			CYLOG(LogPBRT, Error, L"Unhandled texture type: %S", textureType.c_str());
		}
		else
		{
			bValidFormat = false;
			CYLOG(LogPBRT, Error, L"Texture type can be only spectrum or float: %S", textureType.c_str());
		}
	}
	void parseMakeNamedMaterial(std::istream& stream)
	{
		NamedMaterialDesc materialDesc{};
		materialDesc.materialName = readQuoteWord(stream);

		while (true)
		{
			std::string maybeParam = readMaybeQuoteWords(stream);
			if (maybeParam == "string type")
			{
				materialDesc.materialType = readBracketQuoteWord(stream);
			}
			else if (maybeParam == "rgb reflectance")
			{
				materialDesc.rgbReflectance = readBracketVec3(stream);
				materialDesc.bUseRgbReflectance = true;
			}
			else if (maybeParam == "texture reflectance")
			{
				materialDesc.textureReflectance = readBracketQuoteWord(stream);
			}
			else if (maybeParam == "bool remaproughness")
			{
				materialDesc.bRemapRoughness = readBracketQuoteWord(stream) == "true";
			}
			else if (maybeParam == "float roughness")
			{
				materialDesc.roughness = readBracketFloat(stream);
				materialDesc.bUseAnisotropicRoughness = false;
			}
			else if (maybeParam == "float vroughness")
			{
				materialDesc.vroughness = readBracketFloat(stream);
				materialDesc.bUseAnisotropicRoughness = true;
			}
			else if (maybeParam == "float uroughness")
			{
				materialDesc.uroughness = readBracketFloat(stream);
				materialDesc.bUseAnisotropicRoughness = true;
			}
			else if (maybeParam == "spectrum eta")
			{
				materialDesc.spectrumEta = readBracketQuoteWord(stream);
			}
			else if (maybeParam == "spectrum k")
			{
				materialDesc.spectrumK = readBracketQuoteWord(stream);
			}
			else if (maybeParam == "rgb eta")
			{
				materialDesc.rgbEta = readBracketVec3(stream);
				materialDesc.bUseRgbEtaAndK = true;
			}
			else if (maybeParam == "rgb k")
			{
				materialDesc.rgbK = readBracketVec3(stream);
				materialDesc.bUseRgbEtaAndK = true;
			}
			else
			{
				enqueueToken(maybeParam);
				break;
			}
		}
		namedMaterialDescs.emplace_back(materialDesc);
	}
	void parseShape(std::istream& stream)
	{
		if (parsePhase == PBRT4ParsePhase::SceneElements || parsePhase == PBRT4ParsePhase::InsideAttribute)
		{
			std::string shapeType = readQuoteWord(stream);
			if (shapeType == "plymesh")
			{
				// "string filename" [ "models/somefile.ply" ]
				std::string plyFilename, dummyS;
				char dummyC;
				stream >> dummyS >> dummyS >> dummyC >> plyFilename >> dummyC;
				plyFilename = plyFilename.substr(1, plyFilename.size() - 2);

				std::wstring wPlyFilename;
				str_to_wstr(plyFilename, wPlyFilename);

				PLYShapeDesc desc{
					.filename = wPlyFilename,
					.namedMaterial = currentNamedMaterial,
					.transform = currentTransform,
					.bIdentityTransform = bCurrentTransformIsIdentity,
				};
				plyShapeDescs.emplace_back(desc);
			}
			else if (shapeType == "trianglemesh")
			{
				std::vector<vec2> texcoordBuffer;
				std::vector<vec3> normalBuffer;
				std::vector<vec3> positionBuffer;
				std::vector<uint32> indexBuffer;
				while (true)
				{
					std::string maybeParam = readMaybeQuoteWords(stream);
					if (maybeParam == "point2 uv")
					{
						readBracketVec2Array(stream, texcoordBuffer);
					}
					else if (maybeParam == "normal N")
					{
						readBracketVec3Array(stream, normalBuffer);
					}
					else if (maybeParam == "point3 P")
					{
						readBracketVec3Array(stream, positionBuffer);
					}
					else if (maybeParam == "integer indices")
					{
						readBracketUint32Array(stream, indexBuffer);
					}
					else
					{
						enqueueToken(maybeParam);
						break;
					}
				}
				SharedPtr<MaterialAsset> material = makeShared<MaterialAsset>();
				material->emission = currentEmission;

				PBRT4TriangleMesh desc{
					.positionBuffer = std::move(positionBuffer),
					.normalBuffer = std::move(normalBuffer),
					.texcoordBuffer = std::move(texcoordBuffer),
					.indexBuffer = std::move(indexBuffer),
					.material = material,
				};
				triangleShapeDescs.emplace_back(desc);
			}
		}
	}
	void parseAreaLightSource(std::istream& stream)
	{
		std::string lightType = readMaybeQuoteWords(stream);
		if (lightType == "diffuse")
		{
			std::string lightParam = readMaybeQuoteWords(stream);
			if (lightParam == "rgb L")
			{
				currentEmission = readBracketVec3(stream);
			}
			else
			{
				bValidFormat = false;
				CYLOG(LogPBRT, Error, L"Invalid area light param: %S", lightParam.c_str());
			}
		}
		else
		{
			bValidFormat = false;
			CYLOG(LogPBRT, Error, L"Invalid area light type: %S", lightType.c_str());
		}
	}

// Too lazy to write getters for output
public:
	Matrix sceneTransform;
	std::vector<TextureFileDesc> textureFileDescs;
	std::vector<NamedMaterialDesc> namedMaterialDescs;
	std::vector<PBRT4TriangleMesh> triangleShapeDescs;
	std::vector<PLYShapeDesc> plyShapeDescs;

// State machine
private:
	PBRT4ParsePhase parsePhase;
	std::string queuedToken;
	std::string currentNamedMaterial;
	Matrix currentTransform;
	vec3 currentEmission;
	bool bCurrentTransformIsIdentity;
	bool bValidFormat;
};

// -------------------------------------
// PBRT4Scene

PBRT4Scene::~PBRT4Scene()
{
	deallocate();
}

void PBRT4Scene::deallocate()
{
	for (PLYMesh* mesh : plyMeshes)
	{
		delete mesh;
	}
	plyMeshes.clear();
}

// #todo-pbrt: Parser impl roadmap
// 1. Parse geometries and render something.
// 2. Parse light sources and lit the geometries.
// 3. Parse materials and apply to geometries.
PBRT4Scene* PBRT4Loader::loadFromFile(const std::wstring& filepath)
{
	std::wstring wFilepath = ResourceFinder::get().find(filepath);
	if (wFilepath.size() == 0)
	{
		CYLOG(LogPBRT, Error, L"Can't find file: %s", filepath.c_str());
		return nullptr;
	}
	std::filesystem::path baseDirPath = filepath;
	std::wstring baseDir = baseDirPath.parent_path().wstring() + L"/";
	std::fstream fs(wFilepath);
	if (!fs)
	{
		CYLOG(LogPBRT, Error, L"Can't open file: %s", filepath.c_str());
		return nullptr;
	}

	PBRT4Scene* pbrtScene = new PBRT4Scene;
	PBRT4Parser pbrtParser;
	bool bParserValid = pbrtParser.parse(fs, pbrtScene);

	if (pbrtScene == nullptr)
	{
		CYLOG(LogPBRT, Error, L"Failed to parse: %s", filepath.c_str());
		delete pbrtScene;
		return nullptr;
	}

	//std::map<std::string, ImageLoadData*> imageDatabase;
	std::map<std::string, SharedPtr<TextureAsset>> textureAssetDatabase;
	std::map<std::string, SharedPtr<MaterialAsset>> materialDatabase;

	ImageLoader imageLoader;
	for (const TextureFileDesc& desc : pbrtParser.textureFileDescs)
	{
		ImageLoadData* imageBlob = nullptr;

		std::wstring textureFilepath = baseDir + desc.filename;
		textureFilepath = ResourceFinder::get().find(textureFilepath);
		if (textureFilepath.size() > 0)
		{
			imageBlob = imageLoader.load(textureFilepath);
		}

		SharedPtr<TextureAsset> textureAsset;
		if (imageBlob == nullptr)
		{
			textureAsset = gTextureManager->getSystemTextureGrey2D();
		}
		else
		{
			textureAsset = makeShared<TextureAsset>();

			std::wstring wTextureName;
			str_to_wstr(desc.textureName, wTextureName);

			ENQUEUE_RENDER_COMMAND(CreateTextureAsset)(
				[textureAsset, imageBlob, wTextureName](RenderCommandList& commandList)
				{
					TextureCreateParams createParams = TextureCreateParams::texture2D(
						EPixelFormat::R8G8B8A8_UNORM,
						ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
						imageBlob->width, imageBlob->height);
					
					Texture* texture = gRenderDevice->createTexture(createParams);
					texture->uploadData(commandList,
						imageBlob->buffer,
						imageBlob->getRowPitch(),
						imageBlob->getSlicePitch());
					texture->setDebugName(wTextureName.c_str());

					textureAsset->setGPUResource(SharedPtr<Texture>(texture));

					commandList.enqueueDeferredDealloc(imageBlob);
				}
			);
		}

		//imageDatabase.insert(std::make_pair(desc.textureName, imageBlob));
		textureAssetDatabase.insert(std::make_pair(desc.textureName, textureAsset));
	}

	for (const NamedMaterialDesc& desc : pbrtParser.namedMaterialDescs)
	{
		auto material = makeShared<MaterialAsset>();

		material->albedoMultiplier.x = desc.rgbReflectance.x;
		material->albedoMultiplier.y = desc.rgbReflectance.y;
		material->albedoMultiplier.z = desc.rgbReflectance.z;
		if (desc.textureReflectance.size() > 0)
		{
			auto it = textureAssetDatabase.find(desc.textureReflectance);
			if (it != textureAssetDatabase.end())
			{
				material->albedoTexture = it->second;
			}
			else
			{
				CYLOG(LogPBRT, Error, L"Material '%S' uses textureReflectance '%S' but couldn't find it",
					desc.materialName.c_str(), desc.textureReflectance.c_str());
			}
		}
		if (material->albedoTexture == nullptr)
		{
			material->albedoTexture = gTextureManager->getSystemTextureWhite2D();
		}
		
		if (desc.bUseAnisotropicRoughness)
		{
			material->roughness = 0.5f * (desc.uroughness + desc.vroughness);
			CYLOG(LogPBRT, Error, L"Material '%S' uses anisotropic roughness but not supported", desc.materialName.c_str());
		}
		else
		{
			material->roughness = desc.roughness;
		}

		// #todo-material: Parse metallic

		// #todo-pbrt: Other NamedMaterialDesc properties

		materialDatabase.insert(std::make_pair(desc.materialName, material));
	}

	pbrtScene->triangleMeshes = std::move(pbrtParser.triangleShapeDescs);

	PLYLoader plyLoader;
	for (const PLYShapeDesc& desc : pbrtParser.plyShapeDescs)
	{
		std::wstring plyFilepath = baseDir + desc.filename;
		std::wstring plyFullpath = ResourceFinder::get().find(plyFilepath);
		if (plyFullpath.size() == 0)
		{
			CYLOG(LogPBRT, Error, L"Can't find file: %s", plyFilepath.c_str());
		}
		else
		{
			PLYMesh* plyMesh = plyLoader.loadFromFile(plyFullpath);
			if (plyMesh == nullptr)
			{
				CYLOG(LogPBRT, Error, L"Can't parse PLY file: %s", plyFullpath.c_str());
			}
			else
			{
				auto it = materialDatabase.find(desc.namedMaterial);
				if (it != materialDatabase.end())
				{
					plyMesh->material = it->second;
				}
				if (!desc.bIdentityTransform)
				{
					plyMesh->applyTransform(desc.transform);
				}
				pbrtScene->plyMeshes.push_back(plyMesh);
			}
		}
	}
	
	return pbrtScene;
}
