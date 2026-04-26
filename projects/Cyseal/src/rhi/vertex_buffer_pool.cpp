#include "vertex_buffer_pool.h"
#include "core/assertion.h"
#include "core/engine.h"
#include "render_device.h"
#include "buffer.h"
#include "gpu_resource_view.h"

VertexBufferPool* gVertexBufferPool = nullptr;
IndexBufferPool* gIndexBufferPool = nullptr;

// --------------------------------------------------------
// BufferPoolAllocator

// Do [x0, x1) and [y0, y1) intersect?
static bool rangeOverlaps(uint64 x0, uint64 x1, uint64 y0, uint64 y1)
{
	return x1 > y0 && y1 > x0;
}

// #todo-vram-pool: Implement better data structure.
// I have other things to do so here's a super naive linked list approach written in 5 minutes :(
BufferPoolItem BufferPoolAllocator::allocate(uint32 sizeInBytes)
{
	uint64 offset = 0;
	auto it = items.begin();
	for (; it != items.end(); ++it)
	{
		if (rangeOverlaps(offset, offset + sizeInBytes, it->offset, it->offset + it->size))
		{
			offset = it->offset + it->size;
		}
		else
		{
			break;
		}
	}
	if (it != items.end())
	{
		BufferPoolItem item{ offset, sizeInBytes };
		items.insert(it, item);
		bytesAllocated += sizeInBytes;
		return item;
	}
	if (offset + sizeInBytes <= bytesTotal)
	{
		BufferPoolItem item{ offset, sizeInBytes };
		items.push_back(item);
		bytesAllocated += sizeInBytes;
		return item;
	}
	return BufferPoolItem{ 0, 0 };
}


bool BufferPoolAllocator::release(const BufferPoolItem& item)
{
	auto it = std::find(items.begin(), items.end(), item);
	if (it != items.end())
	{
		items.erase(it);
		return true;
	}
	return false;
}

// --------------------------------------------------------
// VertexBufferPool

void VertexBufferPool::initialize(uint64 totalBytes)
{
	CHECK(pool == nullptr);

	const EBufferAccessFlags usageFlags = EBufferAccessFlags::COPY_DST | EBufferAccessFlags::SRV;

	pool = gRenderDevice->createVertexBuffer((uint32)totalBytes, usageFlags, L"GlobalVertexBufferPool");
	
	// Create raw view (ByteAddressBuffer)
	{
		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::R32_TYPELESS;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = (uint32)(totalBytes / 4);
		srvDesc.buffer.structureByteStride = 0;
		srvDesc.buffer.flags               = EBufferSRVFlags::Raw;

		srv = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(pool, srvDesc));
	}

	allocator.initialize(totalBytes);

	const float size_mb = (float)totalBytes / (1024 * 1024);
	CYLOG(LogEngine, Log, L"Vertex buffer pool: %.2f MiB", size_mb);
}

void VertexBufferPool::destroy()
{
	srv.reset();

	CHECK(pool != nullptr);
	delete pool;
	pool = nullptr;
}

VertexBuffer* VertexBufferPool::suballocate(uint32 sizeInBytes)
{
	BufferPoolItem item = allocator.allocate(sizeInBytes);
	if (item.isValid() == false)
	{
		CHECK_NO_ENTRY();
		return nullptr;
	}

	VertexBuffer* buffer = gRenderDevice->createVertexBuffer(this, item.offset, item.size);
	return buffer;
}

bool VertexBufferPool::release(VertexBuffer* buffer)
{
	CHECK(buffer->internal_getParentPool() == this);
	BufferPoolItem item{ buffer->getBufferOffsetInBytes(), buffer->getBufferSizeInBytes() };
	return allocator.release(item);
}

ShaderResourceView* VertexBufferPool::getByteAddressBufferView() const
{
	return srv.get();
}

// --------------------------------------------------------
// IndexBufferPool

void IndexBufferPool::initialize(uint64 totalBytes)
{
	CHECK(pool == nullptr);

	const EBufferAccessFlags usageFlags = EBufferAccessFlags::COPY_DST | EBufferAccessFlags::SRV;

	pool = gRenderDevice->createIndexBuffer(
		(uint32)totalBytes,
		EPixelFormat::R32_UINT,
		usageFlags,
		L"GlobalIndexBufferPool");

	// Create raw view (ByteAddressBuffer)
	{
		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::R32_TYPELESS;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = (uint32)(totalBytes / 4);
		srvDesc.buffer.structureByteStride = 0;
		srvDesc.buffer.flags               = EBufferSRVFlags::Raw;

		srv = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(pool, srvDesc));
	}

	allocator.initialize(totalBytes);

	const float size_mb = (float)totalBytes / (1024 * 1024);
	CYLOG(LogEngine, Log, L"Index buffer pool: %.2f MiB", size_mb);
}

void IndexBufferPool::destroy()
{
	CHECK(pool != nullptr);
	delete pool;
	pool = nullptr;

	srv.reset();
}

IndexBuffer* IndexBufferPool::suballocate(uint32 sizeInBytes, EPixelFormat format)
{
	BufferPoolItem item = allocator.allocate(sizeInBytes);
	if (item.isValid() == false)
	{
		CHECK_NO_ENTRY();
		return nullptr;
	}

	IndexBuffer* buffer = gRenderDevice->createIndexBuffer(this, item.offset, item.size, format);
	return buffer;
}

bool IndexBufferPool::release(IndexBuffer* buffer)
{
	CHECK(buffer->internal_getParentPool() == this);
	BufferPoolItem item{ buffer->getBufferOffsetInBytes(), buffer->getBufferSizeInBytes() };
	return allocator.release(item);
}

ShaderResourceView* IndexBufferPool::getByteAddressBufferView() const
{
	return srv.get();
}
