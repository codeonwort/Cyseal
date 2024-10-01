// Number allocator for something that requires dynamic allocation
// and uses natural numbers as identifiers.

#pragma once

#include "core/int_types.h"

// Possible improvements
// 1. Binary search the intervals to alloc/dealloc.
// 2. Implement a method to allocate several numbers at once.
class FreeNumberList
{
	// Represent allocated numbers
	struct Range { uint32 a, b; Range* next; }; // [a, b]

public:
	FreeNumberList(uint32 inMaxNumber = 0xffffffff)
		: maxNumber(inMaxNumber)
		, head(nullptr)
	{
	}

	~FreeNumberList()
	{
		clear();
	}

	// Allocate a new free number, greater than 0.
	// It's not guaranteed that the smallest free number will be returned.
	// @return The allocated number. 0 if failed.
	uint32 allocate()
	{
		if (head == nullptr)
		{
			head = new Range{ 1, 1, nullptr };
			return head->a;
		}
		else if (head->a > 1)
		{
			head->a -= 1;
			return head->a;
		}

		Range* cand = head;
		while (cand != nullptr)
		{
			if (cand->b == maxNumber) return 0;
			Range* next = cand->next;
			if (next == nullptr || cand->b < next->a - 1)
			{
				cand->b += 1;
				uint32 freeNumber = cand->b;
				if (next != nullptr && cand->b == next->a)
				{
					cand->b = next->b;
					cand->next = next->next;
					delete next;
				}
				return freeNumber;
			}
			cand = cand->next;
		}
		return 0;
	}
	
	//uint32 allocate(uint32 count, uint32* outNumbers);

	// Put back the number to the free list.
	// Fail if it's not a free number.
	// @return true if successful, false otherwise.
	bool deallocate(uint32 number)
	{
		Range* candPrev = nullptr;
		Range* cand = head;
		while (cand != nullptr)
		{
			if (cand->a == number || cand->b == number)
			{
				if (cand->a == number) cand->a += 1;
				else cand->b -= 1;
				if (cand->a > cand->b)
				{
					if (cand == head)
					{
						head = cand->next;
					}
					if (candPrev != nullptr)
					{
						candPrev->next = cand->next;
						delete cand;
					}
				}
				return true;
			}
			else if (cand->a < number && number < cand->b)
			{
				cand->next = new Range{ number + 1, cand->b, cand->next };
				cand->b = number - 1;
				return true;
			}
			candPrev = cand;
			cand = cand->next;
		}
		return false;
	}

	// Returns true if can allocate further.
	bool canAllocate() const
	{
		if (head == nullptr)
		{
			return maxNumber != 0;
		}
		Range* tail = head;
		while (tail != nullptr && tail->next != nullptr)
		{
			if (tail->b < tail->next->a - 1) return true;
			tail = tail->next;
		}
		return tail->b != maxNumber;
	}

	void clear()
	{
		Range* item = head;
		while (item != nullptr)
		{
			Range* temp = item;
			item = item->next;
			delete temp;
		}
		head = nullptr;
	}

private:
	uint32 maxNumber;
	Range* head;
};
