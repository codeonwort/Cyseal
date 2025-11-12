#include "global_descriptor_heaps.h"
#include "render_device.h"
#include "core/assertion.h"

// #todo-renderdevice: Chunk allocators
#define MAX_SRV_DESCRIPTORS 1024
#define MAX_RTV_DESCRIPTORS 64
#define MAX_DSV_DESCRIPTORS 64
#define MAX_UAV_DESCRIPTORS 1024

// gDescriptorHeaps is initialized by gRenderDevice.
GlobalDescriptorHeaps* gDescriptorHeaps = nullptr;

void GlobalDescriptorHeaps::initialize()
{
	srvHeap = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::SRV,
			.numDescriptors = MAX_SRV_DESCRIPTORS,
			.flags          = EDescriptorHeapFlags::None,
			.nodeMask       = 0,
			.purpose        = EDescriptorHeapPurpose::Persistent,
		}
	));

	rtvHeap = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::RTV,
			.numDescriptors = MAX_RTV_DESCRIPTORS,
			.flags          = EDescriptorHeapFlags::None,
			.nodeMask       = 0,
			.purpose        = EDescriptorHeapPurpose::Persistent,
		}
	));

	dsvHeap = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::DSV,
			.numDescriptors = MAX_DSV_DESCRIPTORS,
			.flags          = EDescriptorHeapFlags::None,
			.nodeMask       = 0,
			.purpose        = EDescriptorHeapPurpose::Persistent,
		}
	));

	uavHeap = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::UAV,
			.numDescriptors = MAX_UAV_DESCRIPTORS,
			.flags          = EDescriptorHeapFlags::None,
			.nodeMask       = 0,
			.purpose        = EDescriptorHeapPurpose::Persistent,
		}
	));
}

uint32 GlobalDescriptorHeaps::allocateSRVIndex()
{
	return srvHeap->allocateDescriptorIndex();
}

uint32 GlobalDescriptorHeaps::allocateRTVIndex()
{
	return rtvHeap->allocateDescriptorIndex();
}

uint32 GlobalDescriptorHeaps::allocateDSVIndex()
{
	return dsvHeap->allocateDescriptorIndex();
}

uint32 GlobalDescriptorHeaps::allocateUAVIndex()
{
	return uavHeap->allocateDescriptorIndex();
}
