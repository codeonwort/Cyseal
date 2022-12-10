#include "scene.h"
#include "render/static_mesh.h"

SceneProxy* Scene::createProxy()
{
	SceneProxy* proxy = new SceneProxy;

	// #todo-renderthread: Create proxy data as I did in my OpenGL project
	// but it's really time-consuming and not a high priority in this project.
	// For now just pretend these original data as proxies.
	proxy->sun = sun;
	proxy->skyboxTexture = skyboxTexture ? skyboxTexture->getGPUResource() : nullptr;
	proxy->staticMeshes = staticMeshes;
	proxy->bRebuildRaytracingScene = bRebuildRaytracingScene;

	// Clear flags
	bRebuildRaytracingScene = false;

	return proxy;
}

void Scene::addStaticMesh(StaticMesh* staticMesh)
{
	staticMeshes.push_back(staticMesh);
	bRebuildRaytracingScene = true;
}

void SceneProxy::tempCleanupOriginalScene()
{
	for (StaticMesh* sm : staticMeshes)
	{
		sm->clearDirtyFlags();
	}
}
