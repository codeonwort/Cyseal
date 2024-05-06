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

// D3D12_DESCRIPTOR_HEAP_DESC
struct DescriptorHeapDesc
{
	EDescriptorHeapType type   = EDescriptorHeapType::NUM_TYPES;
	uint32 numDescriptors      = 0;
	EDescriptorHeapFlags flags = EDescriptorHeapFlags::None;
	uint32 nodeMask            = 0; // MGPU thing
};

// ID3D12DescriptorHeap
// VkDescriptorPool
class DescriptorHeap
{
public:
	DescriptorHeap(const DescriptorHeapDesc& inCreateParams)
		: createParams(inCreateParams)
		, freeNumberList(inCreateParams.numDescriptors)
	{
	}

	virtual ~DescriptorHeap() = default;

	virtual void setDebugName(const wchar_t* name) = 0;

	uint32 allocateDescriptorIndex()
	{
		CHECK(currentDescriptorIndex < createParams.numDescriptors);
		uint32 ix = currentDescriptorIndex;
		currentDescriptorIndex += 1;
		return ix;
	}

	// #todo-rhi: DescriptorHeap - support release()

	// #wip-descriptor: Related views must be free'd manually.
	void resetAllDescriptors()
	{
		currentDescriptorIndex = 0;
	}

	const DescriptorHeapDesc& getCreateParams() const { return createParams; }

private:
	const DescriptorHeapDesc createParams;
	FreeNumberList freeNumberList;
	uint32 currentDescriptorIndex = 0;
};
