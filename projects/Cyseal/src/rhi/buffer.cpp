#include "buffer.h"
#include "vertex_buffer_pool.h"

VertexBuffer::~VertexBuffer()
{
	if (parentPool != nullptr)
	{
		parentPool->release(this);
		parentPool = nullptr;
	}
}

IndexBuffer::~IndexBuffer()
{
	if (parentPool != nullptr)
	{
		parentPool->release(this);
		parentPool = nullptr;
	}
}
