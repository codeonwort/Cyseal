#pragma once

#include "gpu_resource.h"
#include "gpu_resource_barrier.h"
#include "barrier_tracker.h"
#include "pixel_format.h"
#include "util/enum_util.h"
#include "core/smart_pointer.h"

class RenderCommandList;

// D3D12_RESOURCE_FLAGS
// VkBufferUsageFlags
enum class EBufferAccessFlags : uint32
{
	NONE          = 0,      // A resource without any flags is no meaningful in any way. Some platform APIs will even report an error.
	COPY_SRC      = 1 << 0, // Can be a source of copy operation (CPU can write data to the buffer)
	COPY_DST      = 1 << 1, // Can be a destination of copy operation
	VERTEX_BUFFER = 1 << 2, // Can be bound as vertex buffer
	INDEX_BUFFER  = 1 << 3, // Can be bound as index buffer
	CBV           = 1 << 4, // Can be bound as CBV
	SRV           = 1 << 5, // Can be bound as SRV
	UAV           = 1 << 6, // Can be bound as UAV
	CPU_WRITE     = 1 << 7, // If set, COPY_DST flag is automatically set.
	CPU_READBACK  = 1 << 8, // If set, COPY_SRC flag is automatically set.
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
	~VertexBuffer();

	void destroy();

	virtual void initialize(uint32 sizeInBytes, EBufferAccessFlags usageFlags) = 0;
	
	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) = 0;

	virtual uint32 getVertexCount() const = 0;

	virtual uint64 getBufferOffsetInBytes() const = 0; // offsetInPool
	virtual uint32 getBufferSizeInBytes() const = 0;
	virtual uint32 getBufferStrideInBytes() const = 0;

	VertexBufferPool* internal_getParentPool() const { return parentPool; }

	virtual uint64 internal_getGPUVirtualAddress() const = 0;

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
	friend class IndexBufferPool;
public:
	~IndexBuffer();

	void destroy();

	virtual void initialize(uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags) = 0;

	virtual void initializeWithinPool(
		IndexBufferPool* pool,
		uint64 offsetInPool,
		uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) = 0;

	virtual uint32 getIndexCount() const = 0;
	virtual EPixelFormat getIndexFormat() const = 0;

	virtual uint64 getBufferOffsetInBytes() const = 0; // offsetInPool
	virtual uint32 getBufferSizeInBytes() const = 0;

	IndexBufferPool* internal_getParentPool() const { return parentPool; }

	virtual uint64 internal_getGPUVirtualAddress() const = 0;

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
	struct ReadbackHandle
	{
		// Do not modify members but only read them.
		bool    bAvailable   = false;
		void*   readbackData = nullptr;
		uint64  readbackSize = 0;
		Buffer* owner        = nullptr;

		~ReadbackHandle()
		{
			if (readbackData != nullptr) delete readbackData;
		}
	};
	static const uint64 READBACK_SIZE_ALL = 0xffffffff;

	void initialize(const BufferCreateParams& inCreateParams)
	{
		createParams = inCreateParams;
		CHECK(createParams.sizeInBytes > 0);

		if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::CPU_WRITE))
		{
			createParams.accessFlags |= EBufferAccessFlags::COPY_DST;
		}
		if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::CPU_READBACK))
		{
			createParams.accessFlags |= EBufferAccessFlags::COPY_SRC;
		}

		lastBarrier = BarrierTracker::BufferState::createUnused();

		onInitialize();
	}

	/// <summary>
	/// Upload data to the internal GPU buffer resource.
	/// This is allowed only if the buffer was initialized with EBufferAccessFlags::CPU_WRITE flag.
	/// </summary>
	/// <param name="commandList"></param>
	/// <param name="numUploads"></param>
	/// <param name="uploadDescs"></param>
	virtual void writeToGPU(RenderCommandList* commandList, uint32 numUploads, Buffer::UploadDesc* uploadDescs) = 0;
	
	void singleWriteToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes, uint64 destOffsetInBytes)
	{
		UploadDesc desc{ srcData, sizeInBytes, destOffsetInBytes };
		writeToGPU(commandList, 1, &desc);
	}

	/// <summary>
	/// Create a request to readback data from GPU.
	/// This is allowed only if the buffer was initialized with EBufferAccessFlags::CPU_READBACK flag.
	/// The data is available when a render device executes the command list and the command queue in the device is flushed.
	/// The returned request could be null if the request failed for somehow.
	/// </summary>
	/// <param name="commandList">Command list in which the request will be processed.</param>
	/// <param name="offset">Offset in bytes to start readback.</param>
	/// <param name="size">Size in bytes to read. Default value means reading from offset to the end.</param>
	/// <returns>Handle to the readback request.</returns>
	virtual SharedPtr<ReadbackHandle> requestReadback(RenderCommandList* commandList, uint64 offset = 0, uint64 size = READBACK_SIZE_ALL) { return nullptr; }

	inline const BufferCreateParams& getCreateParams() const { return createParams; }

	// Use only when a barrier tracker in a command list has no history for this buffer.
	inline const BarrierTracker::BufferState& internal_getLastBarrierState() const { return lastBarrier; }
	// Use only when a command list is closed.
	inline void internal_setLastBarrierState(const BarrierTracker::BufferState& newState) { lastBarrier = newState; }

protected:
	virtual void onInitialize() = 0;

protected:
	BufferCreateParams createParams;

	// This is used only for two cases:
	//   1. Before beginning recording of a command list.
	//   2. After finishing recording of a command list.
	// Intermediate states are tracked by that command list.
	BarrierTracker::BufferState lastBarrier;
};
