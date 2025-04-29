#pragma once

#include "light.h"
#include "gpu_resource_asset.h"
#include "core/smart_pointer.h"
#include <vector>

struct StaticMeshProxy;

// Render thread version of scene representation.
// #todo-renderer: Proxy variants for scene entities.
class SceneProxy
{
public:
	~SceneProxy();

	DirectionalLight              sun;
	SharedPtr<Texture>            skyboxTexture;
	std::vector<StaticMeshProxy*> staticMeshes;

	bool   bRebuildGPUScene        = false;
	bool   bRebuildRaytracingScene = false;
	uint32 totalMeshSectionsLOD0 = 0; // All LOD0 mesh sections of all static meshes in the scene.
};
