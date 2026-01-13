#pragma once

// PBRT v4 file loader
// https://www.pbrt.org/fileformat-v4

#include "pbrt_parser.h"
#include "core/smart_pointer.h"
#include "render/material.h"

#include <string>
#include <vector>
#include <map>

class PLYMesh;

struct PBRT4Scene
{
	// Camera
	vec3 eyePosition;
	vec3 lookAtPosition;
	vec3 upVector;

	std::vector<pbrt::PBRT4ParserOutput::TriangleMeshDesc> triangleMeshes;
	std::vector<PLYMesh*> plyMeshes;

public:
	virtual ~PBRT4Scene();

	void deallocate();
};

class PBRT4Loader
{
public:
	// @return Parsed scene. Should dealloc yourself. Null if load has failed.
	PBRT4Scene* loadFromFile(const std::wstring& filepath);

	MaterialAsset* findNamedMaterial(const char* name) const;

private:
	void loadTextureFiles(const std::wstring& baseDir, const pbrt::PBRT4ParserOutput& parserOutput);
	void loadMaterials(const pbrt::PBRT4ParserOutput& parserOutput);
	void loadPLYMeshes(const std::wstring& baseDir, const std::vector<pbrt::PBRT4ParserOutput::PLYShapeDesc>& descs, std::vector<PLYMesh*>& outMeshes);

	SharedPtr<MaterialAsset> findMaterialByRef(const pbrt::PBRT4MaterialRef& ref) const;

	std::map<std::string, SharedPtr<TextureAsset>> textureAssetDatabase;
	std::map<std::string, SharedPtr<MaterialAsset>> namedMaterialDatabase;
	std::vector<SharedPtr<MaterialAsset>> unnamedMaterialDatabase;
};
