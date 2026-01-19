#pragma once

#include "light.h"
#include "gpu_resource_asset.h"
#include "core/smart_pointer.h"
#include "render/gpu_scene_command.h"

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
	uint32 totalMeshSectionsLOD0   = 0; // All LOD0 mesh sections of all static meshes in the scene.

	uint32 gpuSceneItemMinValidIndex = 0xffffffff;
	uint32 gpuSceneItemMaxValidIndex = 0xffffffff;

public:
	std::vector<GPUSceneEvictCommand>         gpuSceneEvictCommands;
	std::vector<GPUSceneAllocCommand>         gpuSceneAllocCommands;
	std::vector<GPUSceneUpdateCommand>        gpuSceneUpdateCommands;

	std::vector<GPUSceneEvictMaterialCommand> gpuSceneEvictMaterialCommands;
	std::vector<GPUSceneMaterialCommand>      gpuSceneMaterialCommands;
	std::vector<Texture*>                     gpuSceneAlbedoTextures; // For each material command
};
