// Overview
// - Allocate one big vertex buffer, then suballocate on demand.
// - Reduces overallocation by separate committed resources.
// - If a committed resource is really needed, use RenderDevice::createVertexBuffer().
//
// References
// - https://learn.microsoft.com/en-us/windows/win32/direct3d12/large-buffers

#pragma once

#include "gpu_resource.h"
#include "gpu_resource_binding.h"
#include "core/smart_pointer.h"

#include <list>

class VertexBuffer;
class IndexBuffer;
class ShaderResourceView;

// #todo-vram-pool: Implement free list with this.

struct BufferPoolItem
{
	uint64 offset;
	uint32 size;

	inline bool isValid() const { return size > 0; }

	inline bool operator==(const BufferPoolItem& other) const { return offset == other.offset && size == other.size; }
	inline bool operator!=(const BufferPoolItem& other) const { return !(*this == other); }
};

class BufferPoolAllocator
{
public:
	void initialize(uint64 inTotalBytes)
	{
		bytesTotal = inTotalBytes;
		items.clear();
	}

	BufferPoolItem allocate(uint32 sizeInBytes);

	bool release(const BufferPoolItem& item);

	inline uint64 getTotalBytes() const { return bytesTotal; }
	inline uint64 getUsedBytes() const { return bytesAllocated; }
	// Does not guarantee that we can further allocate all of them due to internal fragmentation.
	inline uint64 getAvailableBytes() const { return bytesTotal - bytesAllocated; }

private:
	std::list<BufferPoolItem> items; // ascending order by BufferPoolItem::offset.
	uint64 bytesTotal = 0;
	uint64 bytesAllocated = 0;
};

class VertexBufferPool final
{
public:
	void initialize(uint64 totalBytes);
	void destroy();

	VertexBuffer* suballocate(uint32 sizeInBytes);

	bool release(VertexBuffer* buffer);

	inline uint64 getTotalBytes() const { return allocator.getTotalBytes(); }
	inline uint64 getUsedBytes() const { return allocator.getUsedBytes(); }
	// Does not guarantee that we can further allocate all of them due to internal fragmentation.
	inline uint64 getAvailableBytes() const { return allocator.getAvailableBytes(); }

	ShaderResourceView* getByteAddressBufferView() const;
	
public:
	VertexBuffer* internal_getPoolBuffer() const { return pool; }

private:
	VertexBuffer* pool = nullptr;
	UniquePtr<ShaderResourceView> srv; // ByteAddressBuffer view

	BufferPoolAllocator allocator;
};

class IndexBufferPool final
{
public:
	void initialize(uint64 totalBytes);
	void destroy();

	IndexBuffer* suballocate(uint32 sizeInBytes, EPixelFormat format);

	bool release(IndexBuffer* buffer);

	inline uint64 getTotalBytes() const { return allocator.getTotalBytes(); }
	inline uint64 getUsedBytes() const { return allocator.getUsedBytes(); }
	// Does not guarantee that we can further allocate all of them due to internal fragmentation.
	inline uint64 getAvailableBytes() const { return allocator.getAvailableBytes(); }

	ShaderResourceView* getByteAddressBufferView() const;

public:
	IndexBuffer* internal_getPoolBuffer() const { return pool; }

private:
	IndexBuffer* pool = nullptr;
	UniquePtr<ShaderResourceView> srv; // ByteAddressBuffer view

	BufferPoolAllocator allocator;
};

extern VertexBufferPool* gVertexBufferPool;
extern IndexBufferPool* gIndexBufferPool;
