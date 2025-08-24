#pragma once

#include "core/int_types.h"

class GPUResource;
class Buffer;
class Texture;

// #todo-barrier: Aliasing and UAV barriers?

enum class EBufferMemoryLayout : uint32
{
	COMMON                     = 0,
	PIXEL_SHADER_RESOURCE      = 1,
	UNORDERED_ACCESS           = 2,
	COPY_SRC                   = 3,
	COPY_DEST                  = 4,
	INDIRECT_ARGUMENT          = 5,
};

// VkImageLayout
enum class ETextureMemoryLayout : uint32
{
	COMMON                     = 0,
	RENDER_TARGET              = 1,
	DEPTH_STENCIL_TARGET       = 2,
	PIXEL_SHADER_RESOURCE      = 3,
	UNORDERED_ACCESS           = 4,
	COPY_SRC                   = 5,
	COPY_DEST                  = 6,
	PRESENT                    = 7,
};

// #todo-barrier: Global memory barrier
// VkMemoryBarrier
//struct GlobalMemoryBarrier
//{
//	EGPUResourceState stateBefore;
//	EGPUResourceState stateAfter;
//};

// D3D12_RESOURCE_BARRIER
// VkBufferMemoryBarrier
struct BufferMemoryBarrier
{
	EBufferMemoryLayout stateBefore;
	EBufferMemoryLayout stateAfter;
	Buffer* buffer;
	uint64 offset;
	uint64 size;
};

// D3D12_RESOURCE_BARRIER
// VkImageMemoryBarrier
struct TextureMemoryBarrier
{
	ETextureMemoryLayout stateBefore;
	ETextureMemoryLayout stateAfter;
	GPUResource* texture; // #todo-barrier: The type can't be (Texture*) due to swapchain images.
	uint32 subresource = 0xffffffff; // Index of target subresource. Default is all subresources.
};
