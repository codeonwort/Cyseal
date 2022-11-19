#include "global_descriptor_heaps.h"
#include "render_device.h"
#include "core/assertion.h"

// #todo-renderdevice: Chunk allocators
#define MAX_SRV_DESCRIPTORS 1024
#define MAX_RTV_DESCRIPTORS 64
#define MAX_DSV_DESCRIPTORS 64
#define MAX_UAV_DESCRIPTORS 1024

GlobalDescriptorHeaps* gDescriptorHeaps = nullptr;

void GlobalDescriptorHeaps::initialize()
{
	{
		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::SRV;
		desc.numDescriptors = MAX_SRV_DESCRIPTORS;
		desc.flags          = EDescriptorHeapFlags::None;
		desc.nodeMask       = 0;

		srvHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}
	{
		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::RTV;
		desc.numDescriptors = MAX_RTV_DESCRIPTORS;
		desc.flags          = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		rtvHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}
	{
		DescriptorHeapDesc desc;
		desc.type           = EDescriptorHeapType::DSV;
		desc.numDescriptors = MAX_DSV_DESCRIPTORS;
		desc.flags          = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		dsvHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::UAV;
		desc.numDescriptors = MAX_UAV_DESCRIPTORS;
		desc.flags = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		uavHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}
}

uint32 GlobalDescriptorHeaps::allocateSRVIndex()
{
	CHECK(nextSRVIndex < MAX_SRV_DESCRIPTORS);
	return nextSRVIndex++;
}

uint32 GlobalDescriptorHeaps::allocateRTVIndex()
{
	CHECK(nextSRVIndex < MAX_SRV_DESCRIPTORS);
	return nextSRVIndex++;
}

uint32 GlobalDescriptorHeaps::allocateDSVIndex()
{
	CHECK(nextDSVIndex < MAX_DSV_DESCRIPTORS);
	return nextDSVIndex++;
}

uint32 GlobalDescriptorHeaps::allocateUAVIndex()
{
	CHECK(nextUAVIndex < MAX_UAV_DESCRIPTORS);
	return nextUAVIndex++;
}
