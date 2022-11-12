#include "scene.h"


SceneProxy* Scene::createProxy()
{
	SceneProxy* proxy = new SceneProxy;

	proxy->sun = sun;
	proxy->staticMeshes = staticMeshes;
	proxy->bRebuildRaytracingScene = bRebuildRaytracingScene;

	bRebuildRaytracingScene = false;

	return proxy;
}

void Scene::addStaticMesh(StaticMesh* staticMesh)
{
	staticMeshes.push_back(staticMesh);
	bRebuildRaytracingScene = true;
}
