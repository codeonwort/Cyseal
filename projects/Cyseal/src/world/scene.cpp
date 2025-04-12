#include "scene.h"
#include "scene_proxy.h"
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
	proxy->bRebuildGPUScene = bRebuildGPUScene;
	proxy->bRebuildRaytracingScene = bRebuildRaytracingScene;

	proxy->totalMeshSectionsLOD0 = 0;
	for (StaticMesh* sm : proxy->staticMeshes)
	{
		proxy->totalMeshSectionsLOD0 += (uint32)(sm->getSections(0).size());
	}

	// Clear flags
	bRebuildGPUScene = false;
	bRebuildRaytracingScene = false;

	return proxy;
}

void Scene::addStaticMesh(StaticMesh* staticMesh)
{
	staticMeshes.push_back(staticMesh);
	bRebuildGPUScene = true;
	bRebuildRaytracingScene = true;
}

void SceneProxy::tempCleanupOriginalScene()
{
	for (StaticMesh* sm : staticMeshes)
	{
		sm->clearDirtyFlags();
	}
}
