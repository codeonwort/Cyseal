#include "scene.h"
#include "scene_proxy.h"
#include "render/static_mesh.h"

static uint32 calculateLOD(const StaticMesh* mesh, const Camera& camera)
{
	const size_t numLODs = mesh->getNumLODs();
	float distance = (camera.getPosition() - mesh->getPosition()).length();
	// #todo-lod: Temp criteria
	uint32 lod = 0;
	if (distance >= 90.0f) lod = 3;
	else if (distance >= 60.0f) lod = 2;
	else if (distance >= 30.0f) lod = 1;
	// Clamp LOD
	if (lod >= numLODs) lod = (uint32)(numLODs - 1);
	return lod;
}

void Scene::updateMeshLODs(const Camera& camera, const RendererOptions& rendererOptions)
{
	size_t numStaticMeshes = staticMeshes.size();
	for (uint32 i = 0; i < numStaticMeshes; ++i)
	{
		StaticMesh* sm = staticMeshes[i];
		// #todo-lod: Mesh LOD is currently incompatible with raytracing passes.
		uint32 lod = rendererOptions.anyRayTracingEnabled() ? 0 : calculateLOD(sm, camera);
		sm->setActiveLOD(lod);
	}
}

SceneProxy* Scene::createProxy()
{
	SceneProxy* proxy = new SceneProxy;

	std::vector<StaticMeshProxy*> staticMeshProxyList;
	uint32 totalMeshSectionsLOD0 = 0;
	for (StaticMesh* sm : staticMeshes)
	{
		staticMeshProxyList.push_back(sm->createStaticMeshProxy());
		totalMeshSectionsLOD0 += (uint32)(sm->getSections(0).size());
	}

	proxy->sun                     = sun;
	proxy->skyboxTexture           = skyboxTexture ? skyboxTexture->getGPUResource() : nullptr;
	proxy->staticMeshes            = std::move(staticMeshProxyList);
	proxy->bRebuildGPUScene        = bRebuildGPUScene;
	proxy->bRebuildRaytracingScene = bRebuildRaytracingScene;
	proxy->totalMeshSectionsLOD0   = totalMeshSectionsLOD0;

	// Clear flags
	bRebuildGPUScene = false;
	bRebuildRaytracingScene = false;
	for (StaticMesh* sm : staticMeshes)
	{
		sm->clearDirtyFlags();
		sm->savePrevTransform();
	}

	return proxy;
}

void Scene::addStaticMesh(StaticMesh* staticMesh)
{
	staticMeshes.push_back(staticMesh);
	bRebuildGPUScene = true;
	bRebuildRaytracingScene = true;
}
