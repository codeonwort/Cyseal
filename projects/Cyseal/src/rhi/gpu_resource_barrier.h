#pragma once

// References
// - https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html
// - Vulkanised 2021: Ensure Correct Vulkan Synchronization by Using Synchronization Validation
// - https://gpuopen.com/learn/vulkan-barriers-explained/
// - https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html

// Notes from [Vulkanised 2021]
// Barrier types
// - A memory barrier synchronizes all memory accessible by the GPU.
// - A buffer barrier synchronizes memory access to a buffer.
// - A image barrier synchronizes memory access to an image and allow Image Layout Transitions.
// Image Layout Transitions
// - Rearrange memory for efficient use by different pipeline stages.
// - Happens between the first and second execution scopes of the barrier.
// - Each subresource of an image can be transitioned independently.

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

// ---------------------------------------------------------
// #wip: new types for enhanced barriers

// D3D12_BARRIER_SYNC
enum class EBarrierSync : uint32
{
	NONE = 0,
	ALL = 0x1,
	DRAW = 0x2,
	INDEX_INPUT = 0x4,
	VERTEX_SHADING = 0x8,
	PIXEL_SHADING = 0x10,
	DEPTH_STENCIL = 0x20,
	RENDER_TARGET = 0x40,
	COMPUTE_SHADING = 0x80,
	RAYTRACING = 0x100,
	COPY = 0x200,
	RESOLVE = 0x400,
	EXECUTE_INDIRECT = 0x800,
	PREDICATION = 0x800,
	ALL_SHADING = 0x1000,
	NON_PIXEL_SHADING = 0x2000,
	EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO = 0x4000,
	CLEAR_UNORDERED_ACCESS_VIEW = 0x8000,
	VIDEO_DECODE = 0x100000,
	VIDEO_PROCESS = 0x200000,
	VIDEO_ENCODE = 0x400000,
	BUILD_RAYTRACING_ACCELERATION_STRUCTURE = 0x800000,
	COPY_RAYTRACING_ACCELERATION_STRUCTURE = 0x1000000,
	SPLIT = 0x80000000
};
ENUM_CLASS_FLAGS(EBarrierSync);

// D3D12_BARRIER_ACCESS
enum class EBarrierAccess : uint32
{
	COMMON = 0,
	VERTEX_BUFFER = 0x1,
	CONSTANT_BUFFER = 0x2,
	INDEX_BUFFER = 0x4,
	RENDER_TARGET = 0x8,
	UNORDERED_ACCESS = 0x10,
	DEPTH_STENCIL_WRITE = 0x20,
	DEPTH_STENCIL_READ = 0x40,
	SHADER_RESOURCE = 0x80,
	STREAM_OUTPUT = 0x100,
	INDIRECT_ARGUMENT = 0x200,
	//PREDICATION = 0x200, // #todo-barrier: Conditional rendering
	COPY_DEST = 0x400,
	COPY_SOURCE = 0x800,
	RESOLVE_DEST = 0x1000,
	RESOLVE_SOURCE = 0x2000,
	RAYTRACING_ACCELERATION_STRUCTURE_READ = 0x4000,
	RAYTRACING_ACCELERATION_STRUCTURE_WRITE = 0x8000,
	SHADING_RATE_SOURCE = 0x10000,
	VIDEO_DECODE_READ = 0x20000,
	VIDEO_DECODE_WRITE = 0x40000,
	VIDEO_PROCESS_READ = 0x80000,
	VIDEO_PROCESS_WRITE = 0x100000,
	VIDEO_ENCODE_READ = 0x200000,
	VIDEO_ENCODE_WRITE = 0x400000,
	NO_ACCESS = 0x80000000
};
ENUM_CLASS_FLAGS(EBarrierAccess);

// D3D12_BARRIER_LAYOUT
enum class EBarrierLayout : uint32
{
	Undefined = 0xffffffff,
	Common = 0xfffffffe, // Only this value differs from D3D12_BARRIER_LAYOUT
	Present = 0,
	GenericRead = (Present + 1),
	RenderTarget = (GenericRead + 1),
	UnorderedAccess = (RenderTarget + 1),
	DepthStencilWrite = (UnorderedAccess + 1),
	DepthStencilRead = (DepthStencilWrite + 1),
	ShaderResource = (DepthStencilRead + 1),
	CopySource = (ShaderResource + 1),
	CopyDest = (CopySource + 1),
	ResolveSource = (CopyDest + 1),
	ResolveDest = (ResolveSource + 1),
	ShadingRateSource = (ResolveDest + 1),
	VideoDecodeRead = (ShadingRateSource + 1),
	VideoDecodeWrite = (VideoDecodeRead + 1),
	VideoProcessRead = (VideoDecodeWrite + 1),
	VideoProcessWrite = (VideoProcessRead + 1),
	VideoEncodeRead = (VideoProcessWrite + 1),
	VideoEncodeWrite = (VideoEncodeRead + 1),
	DirectQueueCommon = (VideoEncodeWrite + 1),
	DirectQueueGenericRead = (DirectQueueCommon + 1),
	DirectQueueUnorderedAccess = (DirectQueueGenericRead + 1),
	DirectQueueShaderResource = (DirectQueueUnorderedAccess + 1),
	DirectQueueCopySource = (DirectQueueShaderResource + 1),
	DirectQueueCopyDest = (DirectQueueCopySource + 1),
	ComputeQueueCommon = (DirectQueueCopyDest + 1),
	ComputeQueueGenericRead = (ComputeQueueCommon + 1),
	ComputeQueueUnorderedAccess = (ComputeQueueGenericRead + 1),
	ComputeQueueShaderResource = (ComputeQueueUnorderedAccess + 1),
	ComputeQueueCopySource = (ComputeQueueShaderResource + 1),
	ComputeQueueCopyDest = (ComputeQueueCopySource + 1),
	VideoQueueCommon = (ComputeQueueCopyDest + 1)
};

// D3D12_BARRIER_SUBRESOURCE_RANGE
struct BarrierSubresourceRange
{
	uint32 indexOrFirstMipLevel;
	uint32 numMipLevels;
	uint32 firstArraySlice;
	uint32 numArraySlices;
	uint32 firstPlane;
	uint32 numPlanes;
};

// D3D12_TEXTURE_BARRIER_FLAGS
enum class ETextureBarrierFlags : uint8
{
	None    = 0x0,
	Discard = 0x1,
};
ENUM_CLASS_FLAGS(ETextureBarrierFlags);

struct BufferBarrier
{
	EBarrierSync syncBefore;
	EBarrierSync syncAfter;
	EBarrierAccess accessBefore;
	EBarrierAccess accessAfter;
	GPUResource* buffer; // Must be a buffer
	//uint64 offset => fixed to 0
	//uint64 size => fixed to the buffer size in bytes
};

struct TextureBarrier
{
	EBarrierSync syncBefore;
	EBarrierSync syncAfter;
	EBarrierAccess accessBefore;
	EBarrierAccess accessAfter;
	EBarrierLayout layoutBefore;
	EBarrierLayout layoutAfter;
	GPUResource* texture; // Must be a texture
	BarrierSubresourceRange subresources;
	ETextureBarrierFlags flags;
};

// D3D12_GLOBAL_BARRIER
struct GlobalBarrier
{
	EBarrierSync syncBefore;
	EBarrierSync syncAfter;
	EBarrierAccess accessBefore;
	EBarrierAccess accessAfter;
};
