#pragma once

#include "core/int_types.h"

#include <memory>
#include <vector>

// ------------------------------------------------------------------

template<typename T>
using UniquePtr = std::unique_ptr<T>;

template<typename T>
using SharedPtr = std::shared_ptr<T>;

template<typename T>
using WeakPtr = std::weak_ptr<T>;

template<typename T, typename ...Args>
UniquePtr<T> makeUnique(Args&& ...args)
{
	return std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T, typename ...Args>
SharedPtr<T> makeShared(Args&& ...args)
{
	return std::make_shared<T>(std::forward<Args>(args)...);
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
