#pragma once

#include "gpu_resource.h"
#include "resource_binding.h"

#include <memory>

// Manages GPU memory and descriptor heaps for textures

// todo-wip: Manage committed resources here, not in render device

class TextureManager final
{
public:
	TextureManager();
	~TextureManager();

	void initialize();

	uint32 allocateSRVIndex();

	DescriptorHeap* getSRVHeap() const { return srvHeap.get(); }

private:
	std::unique_ptr<DescriptorHeap> srvHeap;
	uint32 srvIndex;
};

extern TextureManager* gTextureManager;
