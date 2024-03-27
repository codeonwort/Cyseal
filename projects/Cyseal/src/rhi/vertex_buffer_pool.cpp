#include "vertex_buffer_pool.h"
#include "core/assertion.h"
#include "core/engine.h"
#include "render_device.h"
#include "gpu_resource.h"
#include "gpu_resource_view.h"

VertexBufferPool* gVertexBufferPool = nullptr;
IndexBufferPool* gIndexBufferPool = nullptr;

//////////////////////////////////////////////////////////////////////////
// VertexBufferPool

void VertexBufferPool::initialize(uint64 totalBytes)
{
	CHECK(pool == nullptr);

	const EBufferAccessFlags usageFlags = EBufferAccessFlags::COPY_DST | EBufferAccessFlags::SRV;

	poolSize = totalBytes;
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

		srv = std::unique_ptr<ShaderResourceView>(gRenderDevice->createSRV(pool, srvDesc));
	}

	const float size_mb = (float)totalBytes / (1024 * 1024);
	CYLOG(LogEngine, Log, L"Vertex buffer pool: %.2f MiB", size_mb);
}

void VertexBufferPool::destroy()
{
	CHECK(pool != nullptr);
	delete pool;
	pool = nullptr;

	srv.reset();
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

ShaderResourceView* VertexBufferPool::getByteAddressBufferView() const
{
	return srv.get();
}

//////////////////////////////////////////////////////////////////////////
// IndexBufferPool

void IndexBufferPool::initialize(uint64 totalBytes)
{
	CHECK(pool == nullptr);

	const EBufferAccessFlags usageFlags = EBufferAccessFlags::COPY_DST | EBufferAccessFlags::SRV;

	poolSize = totalBytes;
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

		srv = std::unique_ptr<ShaderResourceView>(gRenderDevice->createSRV(pool, srvDesc));
	}

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
	if (currentOffset + sizeInBytes >= poolSize)
	{
		// Out of memory
		CHECK_NO_ENTRY();
		return nullptr;
	}

	IndexBuffer* buffer = gRenderDevice->createIndexBuffer(
		this,
		currentOffset,
		sizeInBytes,
		format);
	
	currentOffset += sizeInBytes;

	return buffer;
}

ShaderResourceView* IndexBufferPool::getByteAddressBufferView() const
{
	return srv.get();
}
