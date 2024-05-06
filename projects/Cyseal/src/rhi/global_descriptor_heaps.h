#pragma once

#include "descriptor_heap.h"
#include "core/smart_pointer.h"

// - Can allocate all types of descriptors.
// - Each render pass will copy the descriptors allocated from here
//   to their volatile heaps.
// - Manages only descriptor heaps. Manage GPU memory for actual resources
//   on your own.
class GlobalDescriptorHeaps
{
public:
	void initialize();

	uint32 allocateSRVIndex();
	uint32 allocateRTVIndex();
	uint32 allocateDSVIndex();
	uint32 allocateUAVIndex();

	// #todo-renderdevice: Free unused descriptors
	//void freeSRVIndex(uint32 index);
	//void freeRTVIndex(uint32 index);
	//void freeDSVIndex(uint32 index);
	//void freeUAVIndex(uint32 index);

	DescriptorHeap* getSRVHeap() const { return srvHeap.get(); }
	DescriptorHeap* getRTVHeap() const { return rtvHeap.get(); }
	DescriptorHeap* getDSVHeap() const { return dsvHeap.get(); }
	DescriptorHeap* getUAVHeap() const { return uavHeap.get(); }

private:
	UniquePtr<DescriptorHeap> srvHeap;
	UniquePtr<DescriptorHeap> rtvHeap;
	UniquePtr<DescriptorHeap> dsvHeap;
	UniquePtr<DescriptorHeap> uavHeap;
};

extern GlobalDescriptorHeaps* gDescriptorHeaps;
