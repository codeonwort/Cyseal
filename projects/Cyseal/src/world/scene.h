#pragma once

#include "light.h"
#include "gpu_resource_asset.h"
#include <vector>
#include <memory>

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
	std::shared_ptr<TextureAsset> skyboxTexture;
private:
	std::vector<StaticMesh*> staticMeshes;
	bool bRebuildGPUScene = false;
	bool bRebuildRaytracingScene = false;
};

//////////////////////////////////////////////////////////////////////////
// Render thread version

// #todo-renderer: Proxy variants for scene entities.
class SceneProxy
{
public:
	// #todo-renderthread: Not needed if there is a real render thread.
	void tempCleanupOriginalScene();

	DirectionalLight sun;
	std::shared_ptr<Texture> skyboxTexture;
	std::vector<StaticMesh*> staticMeshes;

	bool bRebuildGPUScene = false;
	bool bRebuildRaytracingScene = false;
	uint32 totalMeshSectionsLOD0 = 0; // All LOD0 mesh sections of all static meshes in the scene.
};
