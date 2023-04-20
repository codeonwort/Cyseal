#include "pbrt_loader.h"
#include "ply_loader.h"
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

struct PLYShapeDesc
{
	std::wstring filename;
};

std::istream& operator>>(std::istream& stream, vec3& v)
{
	stream >> v.x >> v.y >> v.z;
	return stream;
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

	std::vector<PLYShapeDesc> plyShapeDescs;

	bool bValidFormat = true;
	while (!fs.eof())
	{
		std::string token;
		fs >> token;
		
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
		else if (token == TOKEN_SHAPE)
		{
			if (parsePhase == PBRT4ParsePhase::RenderingOptions)
			{
				bValidFormat = false;
				break;
			}
			else if (parsePhase == PBRT4ParsePhase::SceneElements)
			{
				std::string shapeType;
				fs >> shapeType;
				if (shapeType == "\"plymesh\"")
				{
					// "string filename" [ "models/somefile.ply" ]
					std::string plyFilename, dummyS;
					char dummyC;
					fs >> dummyS >> dummyS >> dummyC >> plyFilename >> dummyC;
					plyFilename = plyFilename.substr(1, plyFilename.size() - 2);

					std::wstring wPlyFilename;
					str_to_wstr(plyFilename, wPlyFilename);

					PLYShapeDesc desc{
						.filename = wPlyFilename
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

	PLYLoader plyLoader;
	for(const PLYShapeDesc& desc : plyShapeDescs)
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
