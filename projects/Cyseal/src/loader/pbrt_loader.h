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
class StaticMesh;

struct PBRT4ObjectInstances
{
	// Object decl
	std::string objectName;
	std::vector<pbrt::PBRT4ParserOutput::TriangleMeshDesc> triangleMeshes;
	std::vector<PLYMesh*> plyMeshes;
	// Instances
	std::vector<Matrix> instanceTransforms;
};

struct PBRT4Scene
{
	// Camera
	vec3 eyePosition;
	vec3 lookAtPosition;
	vec3 upVector;

	std::vector<pbrt::PBRT4ParserOutput::TriangleMeshDesc> triangleMeshes;
	std::vector<PLYMesh*> plyMeshes;
	std::vector<PBRT4ObjectInstances> objectInstances;

public:
	virtual ~PBRT4Scene();

	void deallocate();

	// CAUTION: Deallocate static meshes yourself.
	struct ToCyseal
	{
		std::vector<StaticMesh*> rootObjects;
		std::vector<StaticMesh*> instancedObjects;
	};
	static ToCyseal toCyseal(PBRT4Scene* inoutPbrtScene);
	static StaticMesh* toStaticMesh(std::vector<pbrt::PBRT4ParserOutput::TriangleMeshDesc>& inoutTriangleMeshes, std::vector<PLYMesh*>& inoutPlyMeshes, const SharedPtr<MaterialAsset>& fallbackMaterial);
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
	void loadObjects(const std::wstring& baseDir, pbrt::PBRT4ParserOutput& parserOutput, std::vector<PBRT4ObjectInstances>& outInstances);

	SharedPtr<MaterialAsset> findMaterialByRef(const pbrt::PBRT4MaterialRef& ref) const;

	std::map<std::wstring, SharedPtr<TextureAsset>> textureAssetDatabase; // filename -> texture asset
	std::map<std::string, SharedPtr<TextureAsset>> textureDirectiveDatabase; // texture name -> texture asset
	std::map<std::string, SharedPtr<MaterialAsset>> namedMaterialDatabase;
	std::vector<SharedPtr<MaterialAsset>> unnamedMaterialDatabase;
};
