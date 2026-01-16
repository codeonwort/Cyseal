#pragma once

#include "light.h"
#include "camera.h"
#include "gpu_resource_asset.h"
#include "core/smart_pointer.h"
#include "render/renderer_options.h"
#include <vector>

class StaticMesh;
class SceneProxy;

// Main thread version of scene representation.
class Scene
{
public:
	void updateMeshLODs(const Camera& camera, const RendererOptions& rendererOptions);

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
