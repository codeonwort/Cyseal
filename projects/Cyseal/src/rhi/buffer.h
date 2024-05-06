#pragma once

#include "gpu_resource.h"
#include "util/enum_util.h"

// D3D12_RESOURCE_FLAGS
// VkBufferUsageFlags
// #wip-buffer: more flags and redundant with EGPUResourceState right above
enum class EBufferAccessFlags : uint32
{
	NONE          = 0,
	COPY_SRC      = 1 << 0, // Can be a source of copy operation (CPU can write data to the buffer)
	COPY_DST      = 1 << 1, // Can be a destination of copy operation
	VERTEX_BUFFER = 1 << 2, // Can be bound as vertex buffer
	INDEX_BUFFER  = 1 << 3, // Can be bound as index buffer
	CBV           = 1 << 4, // Can be bound as SRV
	SRV           = 1 << 5, // Can be bound as SRV
	UAV           = 1 << 6, // Can be bound as UAV
};
ENUM_CLASS_FLAGS(EBufferAccessFlags);

struct BufferCreateParams
{
	uint64             sizeInBytes;
	uint32             alignment   = 0;
	EBufferAccessFlags accessFlags = EBufferAccessFlags::NONE;
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
// #todo-rhi: Remove VertexBuffer or make it a child class of Buffer.
class VertexBuffer : public GPUResource
{
	friend class VertexBufferPool;
public:
	virtual void initialize(uint32 sizeInBytes, EBufferAccessFlags usageFlags) = 0;
	
	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) = 0;

	virtual uint32 getVertexCount() const = 0;

	// offsetInPool
	virtual uint64 getBufferOffsetInBytes() const = 0;

	VertexBufferPool* internal_getParentPool() const { return parentPool; }

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

// Can be a committed resource or suballocation of an index buffer pool.
// #todo-rhi: Remove IndexBuffer or make it a child class of Buffer.
class IndexBuffer : public GPUResource
{
public:
	virtual void initialize(uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags) = 0;

	virtual void initializeWithinPool(
		IndexBufferPool* pool,
		uint64 offsetInPool,
		uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) = 0;

	virtual uint32 getIndexCount() const = 0;
	virtual EPixelFormat getIndexFormat() const = 0;

	// offsetInPool
	virtual uint64 getBufferOffsetInBytes() const = 0;

protected:
	// Null if a committed resource.
	IndexBufferPool* parentPool = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// Buffer

// A generic buffer that maintains its own committed resource.
// It's main purpose is to serve GPU memory for various buffer views.
// CBV, SRV, and UAVs can be created from a buffer.
class Buffer : public GPUResource
{
public:
	struct UploadDesc
	{
		void*  srcData;
		uint32 sizeInBytes;
		uint64 destOffsetInBytes;
	};

	virtual void initialize(const BufferCreateParams& inCreateParams)
	{
		createParams = inCreateParams;
		CHECK(createParams.sizeInBytes > 0);
		// ... subclasses do remaining work
	}

	virtual void writeToGPU(RenderCommandList* commandList, uint32 numUploads, Buffer::UploadDesc* uploadDescs) = 0;
	
	void singleWriteToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes, uint64 destOffsetInBytes)
	{
		UploadDesc desc{ srcData, sizeInBytes, destOffsetInBytes };
		writeToGPU(commandList, 1, &desc);
	}

	inline const BufferCreateParams& getCreateParams() const { return createParams; }

protected:
	BufferCreateParams createParams;
};
