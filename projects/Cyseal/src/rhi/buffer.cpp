#include "buffer.h"
#include "vertex_buffer_pool.h"

VertexBuffer::~VertexBuffer()
{
	removeFromPool();
}

bool VertexBuffer::removeFromPool()
{
	if (parentPool != nullptr)
	{
		CHECK(parentPool->release(this));
		parentPool = nullptr;
		bRemovedFromPool = true;
		return true;
	}
	return false;
}

IndexBuffer::~IndexBuffer()
{
	removeFromPool();
}

bool IndexBuffer::removeFromPool()
{
	if (parentPool != nullptr)
	{
		CHECK(parentPool->release(this));
		parentPool = nullptr;
		bRemovedFromPool = true;
		return true;
	}
	return false;
}

