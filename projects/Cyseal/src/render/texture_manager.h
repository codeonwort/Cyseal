#pragma once

#include "gpu_resource.h"
#include "gpu_resource_binding.h"

#include <memory>

class Texture;

// Manages GPU memory and descriptor heaps for textures.
// #todo-wip: (1) Actually not managing texture memory, but only descriptor heaps.
//            (2) StructuredBuffer is abusing this for its SRV and UAV.
class TextureManager final
{
public:
	TextureManager() = default;
	~TextureManager() = default;

	void initialize();
	void destroy();

	uint32 allocateSRVIndex();
	uint32 allocateRTVIndex();
	uint32 allocateDSVIndex();
	uint32 allocateUAVIndex();

	DescriptorHeap* getSRVHeap() const { return srvHeap.get(); }
	DescriptorHeap* getRTVHeap() const { return rtvHeap.get(); }
	DescriptorHeap* getDSVHeap() const { return dsvHeap.get(); }
	DescriptorHeap* getUAVHeap() const { return uavHeap.get(); }

	Texture* getSystemTextureGrey2D() const { return systemTexture_grey2D; }

private:
	void createSystemTextures();

	// Global descriptor pool.
	// Each render pass will create its own volatile heap
	// and copy descriptors from these heaps to their heaps
	// for easy binding of descriptor table.
	std::unique_ptr<DescriptorHeap> srvHeap;
	std::unique_ptr<DescriptorHeap> rtvHeap;
	std::unique_ptr<DescriptorHeap> dsvHeap;
	std::unique_ptr<DescriptorHeap> uavHeap;
	uint32 nextSRVIndex = 0;
	uint32 nextRTVIndex = 0;
	uint32 nextDSVIndex = 0;
	uint32 nextUAVIndex = 0;

	Texture* systemTexture_grey2D = nullptr;
};

extern TextureManager* gTextureManager;
