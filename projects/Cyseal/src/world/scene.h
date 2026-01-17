#pragma once

#include "light.h"
#include "camera.h"
#include "gpu_resource_asset.h"
#include "core/smart_pointer.h"
#include "render/renderer_options.h"

#include <vector>
#include <set>

class StaticMesh;
class SceneProxy;

class GPUSceneItemIndexAllocator
{
public:
	GPUSceneItemIndexAllocator()
		: allocator(0xffffffff, EMemoryTag::World)
	{}

	inline uint32 allocate()
	{
		uint32 n = allocator.allocate() - 1;
		allocatedNumbers.insert(n);
		return n;
	}
	inline bool deallocate(uint32 n)
	{
		allocatedNumbers.erase(n + 1);
		return allocator.deallocate(n + 1);
	}

	inline uint32 getMinValidIndex() const { return (allocatedNumbers.size() > 0) ? *(allocatedNumbers.begin()) : 0xffffffff; }
	inline uint32 getMaxValidIndex() const { return (allocatedNumbers.size() > 0) ? *(allocatedNumbers.rbegin()) : 0xffffffff; }

private:
	FreeNumberList allocator;
	std::set<uint32> allocatedNumbers;
};

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

	GPUSceneItemIndexAllocator gpuSceneItemIndexAllocator;
};
