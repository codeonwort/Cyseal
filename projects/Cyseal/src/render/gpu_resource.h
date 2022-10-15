#pragma once

#include "core/types.h"
#include "util/enum_util.h"
#include "pixel_format.h"

class VertexBufferPool;
class IndexBufferPool;
class RenderCommandList;

// GPU Resources = Buffers + Textures
// #todo: merge with texture.h?

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

enum class EDepthClearFlags : uint8
{
	DEPTH   = 0x1,
	STENCIL = 0x2,
	DEPTH_STENCIL = DEPTH | STENCIL
};
ENUM_CLASS_FLAGS(EDepthClearFlags);

// #todo: Maybe not needed
// Base class for buffers and textures
// ID3D12Resource
class GPUResource
{
};

// #todo-barrier: There are 3 types of barriers (transition, aliasing, and UAV)
// Only deal with transition barrier for now.
enum class EResourceBarrierType
{
	Transition = 0,
	Aliasing = (Transition + 1),
	UAV = (Aliasing + 1)
};
struct ResourceBarrier
{
	const EResourceBarrierType type = EResourceBarrierType::Transition;
	// #todo-barrier: Split barrier
	// ...
	GPUResource* resource;
	EGPUResourceState stateBefore;
	EGPUResourceState stateAfter;
};

//////////////////////////////////////////////////////////////////////////
// Vertex Buffer

struct VertexBufferCreateParams
{
	// Buffer size, must be non-zero.
	uint32 sizeInBytes;

	// If null, the initial data is undefined.
	void* initialData = nullptr;
	// Only meaningful if initialData is there.
	uint32 strideInBytes = 0;

	// If false, this buffer will be suballocated from a global pool.
	// Otherwise, this buffer uses separate allocation.
	// CAUTION: Separate allocation may consume large portion of VRAM
	//          than the buffer actually requires, and there is upper limit
	//          of total allocation count.
	bool bCommittedResource = false;
};

// Can be a committed resource or suballocation of a vertex buffer pool.
class VertexBuffer : public GPUResource
{
	friend class VertexBufferPool;
public:
	virtual void initialize(uint32 sizeInBytes) = 0;
	
	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) = 0;

	VertexBufferPool* internal_getParentPool() { return parentPool; }

protected:
	// Null if a committed resource.
	VertexBufferPool* parentPool = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// Index Buffer

struct IndexBufferCreateParams
{
	uint32 numIndices;
	uint32 elementSize;
};

class IndexBuffer : public GPUResource
{
public:
	virtual void initialize(uint32 sizeInBytes) = 0;

	virtual void initializeWithinPool(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) = 0;

	virtual uint32 getIndexCount() = 0;

protected:
	// Null if a committed resource.
	IndexBufferPool* parentPool = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// Constant Buffer
// D3D12 committed resource (resource + implicit heap)
class ConstantBuffer : public GPUResource
{
public:
	virtual ~ConstantBuffer() = default;

	virtual void clear() = 0;
	virtual void upload(uint32 payloadID, void* payload, uint32 payloadSize) = 0;
};
