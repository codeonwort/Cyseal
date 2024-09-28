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

#include <vector>

class VertexBuffer;
class IndexBuffer;
class ShaderResourceView;

// #todo-vram-pool: Implement free list with this.
struct BufferPoolItem
{
	uint64 offset;
	uint32 size;
};

class VertexBufferPool final
{
public:
	void initialize(uint64 totalBytes);
	void destroy();

	VertexBuffer* suballocate(uint32 sizeInBytes);
	
	// #todo-vram-pool: deallocate()
	// ...

	inline uint64 getTotalBytes() const { return poolSize; }
	inline uint64 getUsedBytes() const { return currentOffset; }
	inline uint64 getAvailableBytes() const { return poolSize - currentOffset; }

	ShaderResourceView* getByteAddressBufferView() const;
	
public:
	VertexBuffer* internal_getPoolBuffer() const { return pool; }

private:
	uint64 poolSize = 0;
	VertexBuffer* pool = nullptr;

	UniquePtr<ShaderResourceView> srv; // ByteAddressBuffer view
	
	// #todo-vram-pool: Only increment for now. Need a free list.
	uint64 currentOffset = 0;
	//std::vector<BufferPoolItem> items;
};

class IndexBufferPool final
{
public:
	void initialize(uint64 totalBytes);
	void destroy();

	IndexBuffer* suballocate(uint32 sizeInBytes, EPixelFormat format);

	// #todo-vram-pool: deallocate()
	// ...

	inline uint64 getTotalBytes() const { return poolSize; }
	inline uint64 getUsedBytes() const { return currentOffset; }
	inline uint64 getAvailableBytes() const { return poolSize - currentOffset; }

	ShaderResourceView* getByteAddressBufferView() const;

public:
	IndexBuffer* internal_getPoolBuffer() const { return pool; }

private:
	uint64 poolSize = 0;
	IndexBuffer* pool = nullptr;

	UniquePtr<ShaderResourceView> srv; // ByteAddressBuffer view

	// #todo-vram-pool: Only increment for now. Need a free list.
	uint64 currentOffset = 0;
	//std::vector<BufferPoolItem> items;
};

extern VertexBufferPool* gVertexBufferPool;
extern IndexBufferPool* gIndexBufferPool;
