#pragma once

// PBRT v4 file loader
// https://www.pbrt.org/fileformat-v4

#include "pbrt_parser.h"
#include "core/smart_pointer.h"
#include "render/material.h"

#include <string>
#include <vector>
#include <map>

struct PBRT4Scene
{
	// Camera
	vec3 eyePosition;
	vec3 lookAtPosition;
	vec3 upVector;

	std::vector<pbrt::PBRT4ParserOutput::TriangleMeshDesc> triangleMeshes;
	std::vector<class PLYMesh*> plyMeshes;

public:
	virtual ~PBRT4Scene();

	void deallocate();
};

class PBRT4Loader
{
public:
	// @return Parsed scene. Should dealloc yourself. Null if load has failed.
	PBRT4Scene* loadFromFile(const std::wstring& filepath);

	MaterialAsset* findLoadedMaterial(const char* name) const;

private:
	std::map<std::string, SharedPtr<TextureAsset>> textureAssetDatabase;
	std::map<std::string, SharedPtr<MaterialAsset>> materialDatabase;
};
