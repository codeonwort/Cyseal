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
	TextureManager();
	~TextureManager();

	void initialize();

	uint32 allocateSRVIndex();

	DescriptorHeap* getSRVHeap() const { return srvHeap.get(); }

	Texture* getSystemTextureGrey2D() const { return systemTexture_grey2D; }

private:
	void createSystemTextures();

	std::unique_ptr<DescriptorHeap> srvHeap;
	uint32 srvIndex;

	Texture* systemTexture_grey2D;
};

extern TextureManager* gTextureManager;
