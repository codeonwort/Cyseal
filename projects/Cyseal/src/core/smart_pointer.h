#pragma once

#include "core/int_types.h"
#include "memory/custom_new_delete.h"

#include <memory>
#include <vector>
#include <limits>
#include <cstdlib>

// ------------------------------------------------------------------

namespace cyseal_private
{
	template<class T>
	struct SharedPtrAllocator
	{
		using value_type = T;

		SharedPtrAllocator() = default;

		SharedPtrAllocator(EMemoryTag inTag) : tag(inTag) {}

		template<class U>
		constexpr SharedPtrAllocator(const SharedPtrAllocator<U>& other) noexcept
		{
			tag = other.getTag();
		}

		[[nodiscard]] T* allocate(std::size_t n)
		{
#if 0
			// Where on the earth Windows.h min and max are smearing???
			constexpr size_t maxAllocSize = std::numeric_limits<std::size_t>::max();
#else
			constexpr size_t maxAllocSize = (std::numeric_limits<std::size_t>::max)();
#endif
			if (n > maxAllocSize / sizeof(T))
			{
				throw std::bad_array_new_length();
			}
			if (auto p = static_cast<T*>(cyseal_private::customMalloc(n * sizeof(T), tag)))
			{
				return p;
			}
			throw std::bad_alloc();
		}

		void deallocate(T* p, std::size_t n) noexcept
		{
			cyseal_private::customFree(p);
		}

		inline EMemoryTag getTag() const { return tag; }

	private:
		EMemoryTag tag = EMemoryTag::Etc;
	};

	template<class T, class U>
	bool operator==(const SharedPtrAllocator<T>&, const SharedPtrAllocator<U>&) { return true; }

	template<class T, class U>
	bool operator!=(const SharedPtrAllocator<T>&, const SharedPtrAllocator<U>&) { return false; }
}

// ------------------------------------------------------------------

template<typename T>
using UniquePtr = std::unique_ptr<T>;

template<typename T>
using SharedPtr = std::shared_ptr<T>;

template<typename T>
using WeakPtr = std::weak_ptr<T>;

template<typename T, EMemoryTag tag = EMemoryTag::Etc, typename ...Args>
UniquePtr<T> makeUnique(Args&& ...args)
{
#if 0
	return std::make_unique<T>(std::forward<Args>(args)...);
#else
	T* raw = new(tag) T(std::forward<Args>(args)...);
	return UniquePtr<T>(raw);
#endif
}

template<typename T, EMemoryTag tag = EMemoryTag::Etc, typename ...Args>
SharedPtr<T> makeShared(Args&& ...args)
{
#if 0
	return std::make_shared<T>(std::forward<Args>(args)...);
#else
	cyseal_private::SharedPtrAllocator<T> allocator(tag);
	return std::allocate_shared<T>(allocator, std::forward<Args>(args)...);
#endif
}

// ------------------------------------------------------------------
// Usually for GPU resources that are instantiated per swapchain.

/// Keep an array of unique pointers internally.
template<typename T>
class BufferedUniquePtr
{
public:
	/// Prepare capacity for internal pointers. at() can access [0, bufferCount - 1] after this.
	void initialize(uint32 bufferCount)
	{
		instances.resize(bufferCount);
	}
	/// Destroy all objects and make internal array of pointers zero sized.
	void clear()
	{
		instances.clear();
	}
	/// Destroy all objects but keep internal array's size.
	void reset()
	{
		for (size_t i = 0u; i < instances.size(); ++i)
		{
			instances[i].reset();
		}
	}
	/// <returns>The number of internal unique pointers. Equals to the argument passed to initialize().</returns>
	size_t size() const
	{
		return instances.size();
	}
	/// <returns>Returns the raw pointer for the given index.</returns>
	T* at(size_t bufferIndex) const
	{
		return instances[bufferIndex].get();
	}
	/// <returns>Returns the raw pointer for the given index.</returns>
	T* at(size_t bufferIndex)
	{
		return instances[bufferIndex].get();
	}
	/// <returns>Returns the unique pointer for the given index.</returns>
	std::unique_ptr<T>& operator[](size_t bufferIndex)
	{
		return instances[bufferIndex];
	}
private:
	std::vector<std::unique_ptr<T>> instances;
};

/// Represents a vector of BufferedUniquePtr, so array of array of unique pointers.
template<typename T>
class BufferedUniquePtrVec
{
public:
	void initialize(uint32 bufferCount)
	{
		instances.resize(bufferCount);
	}
	T* at(size_t bufferIndex, size_t itemIndex)
	{
		return instances[bufferIndex][itemIndex];
	}
	std::vector<std::unique_ptr<T>>& operator[](size_t bufferIndex)
	{
		return instances[bufferIndex];
	}
private:
	std::vector<std::vector<std::unique_ptr<T>>> instances;
};
