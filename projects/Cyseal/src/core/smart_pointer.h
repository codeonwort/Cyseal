#pragma once

#include "core/int_types.h"

#include <memory>
#include <vector>

// ------------------------------------------------------------------
// #todo: Replace all std smart pointer usages with these types.

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

template<typename T>
class BufferedUniquePtr
{
public:
	void initialize(uint32 bufferCount)
	{
		instances.resize(bufferCount);
	}
	T* at(size_t bufferIndex)
	{
		return instances[bufferIndex].get();
	}
	std::unique_ptr<T>& operator[](size_t bufferIndex)
	{
		return instances[bufferIndex];
	}
private:
	std::vector<std::unique_ptr<T>> instances;
};

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
