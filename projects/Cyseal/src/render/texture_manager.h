#pragma once

#include "gpu_resource.h"
#include "resource_binding.h"

#include <memory>

// Manages GPU memory and descriptor heaps for textures

// #todo-wip: Manage committed resources here, not in render device

class Texture;

class TextureManager final
{
public:
	TextureManager() = default;
	~TextureManager() = default;

	void initialize();

	uint32 allocateSRVIndex();
	uint32 allocateRTVIndex();

	DescriptorHeap* getSRVHeap() const { return srvHeap.get(); }
	DescriptorHeap* getRTVHeap() const { return rtvHeap.get(); }

	Texture* getSystemTextureGrey2D() const { return systemTexture_grey2D; }

private:
	void createSystemTextures();

	std::unique_ptr<DescriptorHeap> srvHeap;
	std::unique_ptr<DescriptorHeap> rtvHeap;
	uint32 nextSRVIndex = 0;
	uint32 nextRTVIndex = 0;

	Texture* systemTexture_grey2D = nullptr;
};

extern TextureManager* gTextureManager;
