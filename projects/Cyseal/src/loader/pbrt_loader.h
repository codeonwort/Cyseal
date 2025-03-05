#pragma once

// PBRT v4 file loader
// https://www.pbrt.org/fileformat-v4

#include "core/vec3.h"
#include "core/vec2.h"
#include "core/smart_pointer.h"
#include "render/material.h"

#include <string>
#include <vector>
#include <map>

struct PBRT4TriangleMesh
{
	std::vector<vec3> positionBuffer;
	std::vector<vec3> normalBuffer;
	std::vector<vec2> texcoordBuffer;
	std::vector<uint32> indexBuffer;

	SharedPtr<MaterialAsset> material;
};

struct PBRT4Scene
{
	// Camera
	vec3 eyePosition;
	vec3 lookAtPosition;
	vec3 upVector;

	std::vector<PBRT4TriangleMesh> triangleMeshes;
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

};
