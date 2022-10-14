#include "vertex_buffer_pool.h"
#include "render_device.h"
#include "core/assertion.h"
#include "core/engine.h"
#include "gpu_resource.h"

VertexBufferPool* gVertexBufferPool = nullptr;

void VertexBufferPool::initialize(uint64 totalBytes)
{
	CHECK(pool == nullptr);

	//VertexBufferCreateParams desc;
	//desc.sizeInBytes = totalBytes;
	//desc.initialData = nullptr;
	//desc.bCommittedResource = true;

	poolSize = totalBytes;
	pool = gRenderDevice->createVertexBuffer((uint32)totalBytes, L"GlobalVertexBufferPool");
	
	const float size_mb = (float)totalBytes / (1024 * 1024);
	CYLOG(LogEngine, Log, L"Vertex buffer pool: %.2f MiB", size_mb);
}

void VertexBufferPool::destroy()
{
	CHECK(pool != nullptr);
	delete pool;
	pool = nullptr;
}

VertexBuffer* VertexBufferPool::suballocate(uint32 sizeInBytes)
{
	if (currentOffset + sizeInBytes >= poolSize)
	{
		// Out of memory
		CHECK_NO_ENTRY();
		return nullptr;
	}

	VertexBuffer* buffer = gRenderDevice->createVertexBuffer(this, currentOffset, sizeInBytes);
	currentOffset += sizeInBytes;

	return buffer;
}
