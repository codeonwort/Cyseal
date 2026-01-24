#pragma once

#include "core/int_types.h"
#include "core/assertion.h"
#include "memory/free_number_list.h"

// D3D12_DESCRIPTOR_HEAP_TYPE
// VkDescriptorType
enum class EDescriptorHeapType : uint8
{
	CBV         = 0,
	SRV         = 1,
	UAV         = 2,
	CBV_SRV_UAV = 3,
	SAMPLER     = 4,
	RTV         = 5,
	DSV         = 6,
	NUM_TYPES   = 7
};

// D3D12_DESCRIPTOR_HEAP_FLAGS
enum class EDescriptorHeapFlags : uint8
{
	None          = 0,
	ShaderVisible = 1,
};

// Not directly mapped to DX12 or Vulkan API, but for Cyseal's architecture.
enum class EDescriptorHeapPurpose : uint8
{
	Persistent, // For global descriptors. The heap will remain persistent.
	Volatile,   // For per-frame descriptor. The heap will be cleared and updated every frame.
};

// D3D12_DESCRIPTOR_HEAP_DESC
// VkDescriptorPoolCreateInfo
struct DescriptorHeapDesc
{
	EDescriptorHeapType    type;
	uint32                 numDescriptors;
	EDescriptorHeapFlags   flags;
	uint32                 nodeMask; // MGPU thing

	EDescriptorHeapPurpose purpose;
};

struct DescriptorIndexTracker
{
	int32 lastIndex = 0;
};

// ID3D12DescriptorHeap
// VkDescriptorPool
class DescriptorHeap
{
public:
	DescriptorHeap(const DescriptorHeapDesc& inCreateParams)
		: createParams(inCreateParams)
		, freeNumberList(inCreateParams.numDescriptors, EMemoryTag::RHI)
	{
	}

	virtual ~DescriptorHeap() = default;

	virtual void setDebugName(const wchar_t* name) = 0;

	uint32 allocateDescriptorIndex()
	{
		uint32 ix = freeNumberList.allocate();
		CHECK(ix != 0);
		return ix - 1;
	}

	// #todo-rhi: Related views (SRV, RTV, ...) must be free'd manually. More convenient way?
	bool releaseDescriptorIndex(uint32 index)
	{
		return freeNumberList.deallocate(index + 1);
	}

	void resetAllDescriptors()
	{
		freeNumberList.clear();
	}

	// #todo-gpuscene: Super bad API design. See RenderDevice::internal_cloneSRVWithDifferentHeap().
	void internal_copyAllDescriptorIndicesFrom(const DescriptorHeap* src)
	{
		FreeNumberList::clone(src->freeNumberList, freeNumberList);
	}

	const DescriptorHeapDesc& getCreateParams() const { return createParams; }

private:
	const DescriptorHeapDesc createParams;
	FreeNumberList freeNumberList;
};
