// Overview
// - Allocate one big vertex buffer, then suballocate on demand.
// - Reduces overallocation by separate committed resources.
// - If a committed resource is really needed, use RenderDevice::createVertexBuffer().
//
// References
// - https://learn.microsoft.com/en-us/windows/win32/direct3d12/large-buffers

#pragma once

#include "gpu_resource.h"
#include "resource_binding.h"

#include <memory>
#include <vector>

struct VertexBufferPoolItem
{
	uint64 offset;
	uint64 size;
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
	
public:
	VertexBuffer* internal_getPoolBuffer() const { return pool; }

private:
	uint64 poolSize = 0;
	VertexBuffer* pool = nullptr;
	
	// #todo-vram-pool: Only increment for now. Need a free list.
	uint64 currentOffset = 0;
	std::vector<VertexBufferPoolItem> items;
};

extern VertexBufferPool* gVertexBufferPool;
