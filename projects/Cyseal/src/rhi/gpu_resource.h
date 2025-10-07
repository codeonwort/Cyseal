#pragma once

#include "core/types.h"
#include "core/assertion.h"

// GPU Resources = Anything that resides in GPU-visible memory.
// (buffers, textures, acceleration structures, ...)

// Base class for GPU resources (buffers, textures, accel structs, ...)
// ID3D12Resource
class GPUResource
{
public:
	virtual ~GPUResource() {}

	// D3D12: ID3D12Resource
	// Vulkan: VkBuffer or VkImage
	virtual void* getRawResource() const { CHECK_NO_ENTRY(); return nullptr; }
	virtual void setRawResource(void* inRawResource) { CHECK_NO_ENTRY(); }

	virtual void setDebugName(const wchar_t* inDebugName) {}
};
