#pragma once

#include "core/types.h"
#include "core/assertion.h"

// GPU Resources = Anything resides in GPU-visible memory
// (buffers, textures, acceleration structures, ...)

// #todo-rhi: Not good abstraction for Vulkan
// D3D12_RESOURCE_STATES
enum class EGPUResourceState : uint32
{
	COMMON                     = 0,
	VERTEX_AND_CONSTANT_BUFFER = 0x1,
	INDEX_BUFFER               = 0x2,
	RENDER_TARGET              = 0x4,
	UNORDERED_ACCESS           = 0x8,
	DEPTH_WRITE                = 0x10,
	DEPTH_READ                 = 0x20,
	NON_PIXEL_SHADER_RESOURCE  = 0x40,
	PIXEL_SHADER_RESOURCE      = 0x80,
	STREAM_OUT                 = 0x100,
	INDIRECT_ARGUMENT          = 0x200,
	COPY_DEST                  = 0x400,
	COPY_SOURCE                = 0x800,
	RESOLVE_DEST               = 0x1000,
	RESOLVE_SOURCE             = 0x2000,
	//GENERIC_READ             = (((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
	PRESENT                    = 0,
	PREDICATION                = 0x200,
	VIDEO_DECODE_READ          = 0x10000,
	VIDEO_DECODE_WRITE         = 0x20000,
	VIDEO_PROCESS_READ         = 0x40000,
	VIDEO_PROCESS_WRITE        = 0x80000
};

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
