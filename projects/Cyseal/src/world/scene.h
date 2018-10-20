#pragma once

#include <vector>

class StaticMesh;
class SceneProxy;

class Scene
{

public:
	SceneProxy* createProxy();

	void addStaticMesh(StaticMesh* staticMesh);

private:
	std::vector<StaticMesh*> staticMeshes;

};

//////////////////////////////////////////////////////////////////////////
// Render thread version

class SceneProxy
{
	
public:
	std::vector<StaticMesh*> staticMeshes;

};
