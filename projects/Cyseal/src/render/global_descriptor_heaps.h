#pragma once

#include "gpu_resource_binding.h"
#include <memory>

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
	std::unique_ptr<DescriptorHeap> srvHeap;
	std::unique_ptr<DescriptorHeap> rtvHeap;
	std::unique_ptr<DescriptorHeap> dsvHeap;
	std::unique_ptr<DescriptorHeap> uavHeap;

	uint32 nextSRVIndex = 0;
	uint32 nextRTVIndex = 0;
	uint32 nextDSVIndex = 0;
	uint32 nextUAVIndex = 0;
};

extern GlobalDescriptorHeaps* gDescriptorHeaps;
