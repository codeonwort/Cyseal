#pragma once

#include "light.h"
#include "gpu_resource_asset.h"
#include "core/smart_pointer.h"
#include <vector>

class StaticMesh;
class SceneProxy;

// Main thread version of scene representation.
class Scene
{
public:
	SceneProxy* createProxy();

	void addStaticMesh(StaticMesh* staticMesh);

public:
	DirectionalLight sun;
	SharedPtr<TextureAsset> skyboxTexture;
private:
	std::vector<StaticMesh*> staticMeshes;
	bool bRebuildGPUScene = false;
	bool bRebuildRaytracingScene = false;
};
