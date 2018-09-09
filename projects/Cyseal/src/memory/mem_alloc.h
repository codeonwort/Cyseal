#pragma once

#include <stdint.h>
#include <stdlib.h>
#include "core/assertion.h"

// Custom memory allocators.

class StackAllocator
{

public:
	explicit StackAllocator(uint32_t bytes);
	~StackAllocator();

	void* alloc(uint32_t bytes);
	void clear();

private:
	void* memblock;
	void* current;
	uint32_t totalBytes;
	uint32_t usedBytes;

};

template<typename T>
class PoolAllocator
{
	struct FreeNode
	{
		T element;
		FreeNode* next;
	};
	
public:
	explicit PoolAllocator(uint32_t numElements)
	{
		memblock = malloc(numElements * sizeof(FreeNode));
		void* current = memblock;
		FreeNode* prev = nullptr;
		for (auto i = 0u; i < numElements; ++i)
		{
			FreeNode* node = reinterpret_cast<FreeNode*>(current);
			current = reinterpret_cast<uint8_t*>(current) + sizeof(FreeNode);
			new (&(node->element)) T();
			node->next = prev;
			prev = node;
		}
		freeList = prev;
	}
	~PoolAllocator()
	{
		free(memblock);
	}

	T* alloc()
	{
		if (freeList == nullptr)
		{
			return nullptr;
		}
		T* elem = &(freeList->element);
		freeList = freeList->next;
		return elem;
	}

	void dealloc(T* element)
	{
		FreeNode* node = reinterpret_cast<FreeNode*>(element);
		node->next = freeList;
		freeList = node;
	}

private:
	void* memblock;
	FreeNode* freeList;

};
