#pragma once

// PBRT v4 file loader
// https://www.pbrt.org/fileformat-v4

#include <string>
#include <vector>
#include "core/vec3.h"

struct PBRT4Scene
{
	// Camera
	vec3 eyePosition;
	vec3 lookAtPosition;
	vec3 upVector;

	std::vector<class PLYMesh*> plyMeshes;

	void deallocate();
};

class PBRT4Loader
{
public:
	// @return Parsed scene. Should dealloc yourself. Null if load has failed.
	PBRT4Scene* loadFromFile(const std::wstring& filepath);

private:

};
