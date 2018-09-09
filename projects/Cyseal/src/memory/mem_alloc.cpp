#include "mem_alloc.h"
#include "core/assertion.h"

StackAllocator::StackAllocator(uint32_t bytes)
{
	totalBytes = bytes;
	memblock = malloc(bytes);
	usedBytes = 0;
}

StackAllocator::~StackAllocator()
{
	free(memblock);
}

void* StackAllocator::alloc(uint32_t bytes)
{
	CHECK(bytes > 0);

	if (usedBytes + bytes > totalBytes)
	{
		return nullptr;
	}

	void* block = (void*)(reinterpret_cast<uint8_t*>(memblock) + usedBytes);
	usedBytes += bytes;

	return block;
}

void StackAllocator::clear()
{
	usedBytes = 0;
}
