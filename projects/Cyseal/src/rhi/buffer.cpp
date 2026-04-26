#include "buffer.h"
#include "vertex_buffer_pool.h"

VertexBuffer::~VertexBuffer()
{
	destroy();
}

void VertexBuffer::destroy()
{
	if (parentPool != nullptr)
	{
		CHECK(parentPool->release(this));
		parentPool = nullptr;
	}
}

IndexBuffer::~IndexBuffer()
{
	destroy();
}

void IndexBuffer::destroy()
{
	if (parentPool != nullptr)
	{
		CHECK(parentPool->release(this));
		parentPool = nullptr;
	}
}
