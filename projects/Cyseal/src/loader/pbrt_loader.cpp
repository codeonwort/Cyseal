#include "pbrt_loader.h"
#include "ply_loader.h"
#include "core/assertion.h"
#include "util/resource_finder.h"
#include "util/string_conversion.h"
#include "util/logging.h"

#include <vector>
#include <fstream>
#include <filesystem>

#define TOKEN_LOOKAT "LookAt"
#define TOKEN_CAMERA "Camera"
#define TOKEN_SAMPLER "Sampler"
#define TOKEN_INTEGRATOR "Integrator"
#define TOKEN_FILM "Film"

#define TOKEN_WORLDBEGIN "WorldBegin"

#define TOKEN_LIGHTSOURCE "LightSource"
#define TOKEN_ATTRIBUTEBEGIN "AttributeBegin"
#define TOKEN_ATTRIBUTEEND "AttributeEnd"
#define TOKEN_MATERIAL "Material"
#define TOKEN_NAMEDMATERIAL "NamedMaterial"
#define TOKEN_MAKENAMEDMATERIAL "MakeNamedMaterial"
#define TOKEN_SHAPE "Shape"
#define TOKEN_TEXTURE "Texture"
#define TOKEN_TRANSLATE "Translate"

DEFINE_LOG_CATEGORY_STATIC(LogPBRT);

enum class PBRT4ParsePhase
{
	RenderingOptions = 0,
	SceneElements = 1,
	Attribute = 2,
};

struct TextureFileDesc
{
	std::string textureName;
	std::string textureFilter; // #todo-pbrt
	std::string filename;
};

struct NamedMaterialDesc
{
	std::string materialName;
	std::string materialType;

	bool bUseRgbReflectance = false;
	vec3 rgbReflectance;
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
	std::string namedMaterial;
};

std::istream& operator>>(std::istream& stream, vec3& v)
{
	stream >> v.x >> v.y >> v.z;
	return stream;
}

std::string readQuoteWord(std::istream& stream)
{
	std::string str;
	stream >> str;
	return str.substr(1, str.size() - 2);
}
std::string readQuoteTwoWords(std::istream& stream)
{
	std::string word1, word2;
	stream >> word1 >> word2;
	return word1.substr(1) + " " + word2.substr(0, word2.size() - 1);
}
std::string readBracketQuoteWord(std::istream& stream)
{
	char ch;
	stream >> ch;
	std::string str = readQuoteWord(stream);
	stream >> ch;
	return str;
}
float readBracketFloat(std::istream& stream)
{
	char ch;
	float x;
	stream >> ch >> x >> ch;
	return x;
}
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
	PBRT4ParsePhase parsePhase = PBRT4ParsePhase::RenderingOptions;

	// ------------------------------------------------
	// Steps
	// 1. Rendering options
	// 2. WorldBegin
	// 3. Lights, geometries, and volumes

	std::vector<TextureFileDesc> textureFileDescs;
	std::vector<NamedMaterialDesc> namedMaterialDescs;
	std::vector<PLYShapeDesc> plyShapeDescs;

	// State machine
	std::string queuedToken;
	std::string currentNamedMaterial;
	bool bValidFormat = true;

	auto enqueueToken = [&queuedToken](const std::string& tok)
	{
		CHECK(queuedToken.size() == 0);
		queuedToken = tok;
	};

	while (!fs.eof())
	{
		std::string token;
		if (queuedToken.size() > 0)
		{
			token = queuedToken;
			queuedToken = "";
		}
		else
		{
			fs >> token;
		}
		
		if (token == TOKEN_LOOKAT)
		{
			if (parsePhase == PBRT4ParsePhase::RenderingOptions)
			{
				fs >> pbrtScene->eyePosition;
				fs >> pbrtScene->lookAtPosition;
				fs >> pbrtScene->upVector;
			}
			else
			{
				bValidFormat = false;
				break;
			}
		}
		else if (token == TOKEN_CAMERA)
		{
			// #todo-pbrt: Parse token Camera
			int z = 0;
		}
		else if (token == TOKEN_SAMPLER)
		{
			// #todo-pbrt: Parse token Sampler
		}
		else if (token == TOKEN_INTEGRATOR)
		{
			// #todo-pbrt: Parse token Integrator
		}
		else if (token == TOKEN_FILM)
		{
			// #todo-pbrt: Parse token Film
		}
		else if (token == TOKEN_WORLDBEGIN)
		{
			if (parsePhase == PBRT4ParsePhase::RenderingOptions)
			{
				parsePhase = PBRT4ParsePhase::SceneElements;
			}
			else
			{
				bValidFormat = false;
				break;
			}
		}
		else if (token == TOKEN_TEXTURE)
		{
			std::string textureName = readQuoteWord(fs);
			std::string textureType = readQuoteWord(fs);
			if (textureType == "spectrum")
			{
				std::string textureClass = readQuoteWord(fs);
				if (textureClass == "imagemap")
				{
					std::string textureFilter, textureFilename;
					for (uint32 i = 0; i < 2; ++i)
					{
						std::string paramName = readQuoteTwoWords(fs);
						if (paramName == "string filter")
						{
							// #todo-pbrt: texture filter
							textureFilter = readQuoteWord(fs);
						}
						else if (paramName == "string filename")
						{
							textureFilename = readBracketQuoteWord(fs);
						}
					}
					TextureFileDesc desc{
						.textureName = textureName,
						.textureFilter = textureFilter,
						.filename = textureFilename
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
		else if (token == TOKEN_MAKENAMEDMATERIAL)
		{
			NamedMaterialDesc materialDesc{};
			materialDesc.materialName = readQuoteWord(fs);
			while (true)
			{
				std::string maybeParam = readMaybeQuoteWords(fs);
				if (maybeParam == "string type")
				{
					materialDesc.materialType = readBracketQuoteWord(fs);
				}
				else if (maybeParam == "rgb reflectance")
				{
					materialDesc.rgbReflectance = readBracketVec3(fs);
					materialDesc.bUseRgbReflectance = true;
				}
				else if (maybeParam == "texture reflectance")
				{
					materialDesc.textureReflectance = readBracketQuoteWord(fs);
				}
				else if (maybeParam == "bool remaproughness")
				{
					materialDesc.bRemapRoughness = readBracketQuoteWord(fs) == "true";
				}
				else if (maybeParam == "float roughness")
				{
					materialDesc.roughness = readBracketFloat(fs);
					materialDesc.bUseAnisotropicRoughness = false;
				}
				else if (maybeParam == "float vroughness")
				{
					materialDesc.vroughness = readBracketFloat(fs);
					materialDesc.bUseAnisotropicRoughness = true;
				}
				else if (maybeParam == "float uroughness")
				{
					materialDesc.uroughness = readBracketFloat(fs);
					materialDesc.bUseAnisotropicRoughness = true;
				}
				else if (maybeParam == "spectrum eta")
				{
					materialDesc.spectrumEta = readBracketQuoteWord(fs);
				}
				else if (maybeParam == "spectrum k")
				{
					materialDesc.spectrumK = readBracketQuoteWord(fs);
				}
				else if (maybeParam == "rgb eta")
				{
					materialDesc.rgbEta = readBracketVec3(fs);
					materialDesc.bUseRgbEtaAndK = true;
				}
				else if (maybeParam == "rgb k")
				{
					materialDesc.rgbK = readBracketVec3(fs);
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
		else if (token == TOKEN_NAMEDMATERIAL)
		{
			currentNamedMaterial = readQuoteWord(fs);
		}
		else if (token == TOKEN_SHAPE)
		{
			if (parsePhase == PBRT4ParsePhase::RenderingOptions)
			{
				bValidFormat = false;
				break;
			}
			else if (parsePhase == PBRT4ParsePhase::SceneElements)
			{
				std::string shapeType = readQuoteWord(fs);
				if (shapeType == "plymesh")
				{
					// "string filename" [ "models/somefile.ply" ]
					std::string plyFilename, dummyS;
					char dummyC;
					fs >> dummyS >> dummyS >> dummyC >> plyFilename >> dummyC;
					plyFilename = plyFilename.substr(1, plyFilename.size() - 2);

					std::wstring wPlyFilename;
					str_to_wstr(plyFilename, wPlyFilename);

					PLYShapeDesc desc{
						.filename = wPlyFilename,
						.namedMaterial = currentNamedMaterial,
					};
					plyShapeDescs.emplace_back(desc);
				}
			}
		}
		else if (token == TOKEN_LIGHTSOURCE)
		{
			// #todo-pbrt: Parse token LightSource
			int z = 0;
		}
		else if (token == TOKEN_ATTRIBUTEBEGIN)
		{
			// #todo-pbrt: Parse token AttributeBegin
			int z = 0;
		}
		else if (token == TOKEN_ATTRIBUTEEND)
		{
			// #todo-pbrt: Parse token AttributeEnd
			int z = 0;
		}
	}

	if (!bValidFormat)
	{
		CYLOG(LogPBRT, Error, L"Failed to parse: %s", filepath.c_str());
		delete pbrtScene;
		return nullptr;
	}

	// #wip: Load texture files
	{
		textureFileDescs;
	}

	// #wip: Materials
	{
		namedMaterialDescs;
	}

	PLYLoader plyLoader;
	for (const PLYShapeDesc& desc : plyShapeDescs)
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
				pbrtScene->plyMeshes.push_back(plyMesh);
			}
		}
	}
	
	return pbrtScene;
}
