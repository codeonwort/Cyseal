#pragma once

#include "light.h"
#include <vector>

class StaticMesh;
class SceneProxy;

//////////////////////////////////////////////////////////////////////////
// Main thread version

class Scene
{
public:
	SceneProxy* createProxy();

	void addStaticMesh(StaticMesh* staticMesh);

public:
	DirectionalLight sun;
private:
	std::vector<StaticMesh*> staticMeshes;
};

//////////////////////////////////////////////////////////////////////////
// Render thread version

// #todo-renderer: Proxy variants for scene entities.
class SceneProxy
{
public:
	DirectionalLight sun;
	std::vector<StaticMesh*> staticMeshes;
};
