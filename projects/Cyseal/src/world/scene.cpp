#include "scene.h"


SceneProxy* Scene::createProxy()
{
	SceneProxy* proxy = new SceneProxy;

	proxy->sun = sun;
	proxy->staticMeshes = staticMeshes;

	return proxy;
}

void Scene::addStaticMesh(StaticMesh* staticMesh)
{
	staticMeshes.push_back(staticMesh);
}
