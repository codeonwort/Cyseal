#pragma once

#include "render/gpu_resource.h"
#include "render/resource_binding.h"
#include "core/assertion.h"
#include "d3d_util.h"

// #todo: Maybe not needed
class D3DResource : public GPUResource
{

public:
	inline ID3D12Resource* getRaw() const { return rawResource; }
	inline void setRaw(ID3D12Resource* raw) { rawResource = raw; }
	
protected:
	ID3D12Resource* rawResource;

};

// --------------------------------------------------------------

// https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12descriptorheap
class D3DDescriptorHeap : public DescriptorHeap
{
public:
	void initialize(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
	{
		HR( device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rawState)) );
	}

	ID3D12DescriptorHeap* getRaw() const { return rawState.Get(); }

private:
	WRL::ComPtr<ID3D12DescriptorHeap> rawState;
};

// https://www.braynzarsoft.net/viewtutorial/q16390-directx-12-constant-buffers-root-descriptor-tables
class D3DConstantBuffer : public ConstantBuffer
{
public:
	virtual void clear()
	{
		// Not needed, but for clarity.
		::memset(mapPtr, 0, resourceHeapSize);
	}

	virtual void upload(uint32 payloadID, void* payload, uint32 payloadSize)
	{
		CHECK(payloadID < payloadMaxCount);

		::memcpy_s(mapPtr + payloadID * payloadSizeAligned, payloadSize, payload, payloadSize);
	}

	void initialize(
		ID3D12Device*         device,
		ID3D12DescriptorHeap* descriptorHeap,
		uint32                heapSize,
		uint32                payloadSize)
	{
		CHECK(heapSize > 0 && payloadSize > 0);
		CHECK(heapSize % (1024 * 64) == 0);

		resourceHeapSize = heapSize;
		payloadSizeAligned = (payloadSize + 255) & ~255;

		// #todo: It consumes too much memory
		// Create a committed resource
		HR( device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(heapSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&rawResource)) );

		// Create CBVs
		payloadMaxCount = resourceHeapSize / payloadSizeAligned;
		for (uint32 payloadId = 0; payloadId < payloadMaxCount; ++payloadId)
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
			viewDesc.BufferLocation = rawResource->GetGPUVirtualAddress() + (payloadId * payloadSizeAligned);
			viewDesc.SizeInBytes = payloadSizeAligned;

			D3D12_CPU_DESCRIPTOR_HANDLE descHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
			UINT descHandleInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			descHandle.ptr += payloadId * descHandleInc;

			device->CreateConstantBufferView(&viewDesc, descHandle);
		}

		// No read from CPU
		CD3DX12_RANGE readRange(0, 0);
		HR( rawResource->Map(0, &readRange, reinterpret_cast<void**>(&mapPtr)) );
		CHECK(mapPtr != nullptr);
	}

	void destroy()
	{
		if (rawResource != nullptr)
		{
			CD3DX12_RANGE readRange(0, 0);
			rawResource->Unmap(0, &readRange);

			mapPtr = nullptr;
		}
	}

private:
	// todo-wip: Constant buffer object itself is a committed resource
	// but CBVs are allocated in a cbv heap. Who is responsible for management of the cbv heap?
	WRL::ComPtr<ID3D12Resource> rawResource;

	uint32 resourceHeapSize = 0; // The size of implicit heap of committed resource
	uint32 payloadSizeAligned = 0; // 256-bytes aligned
	uint32 payloadMaxCount = 0;
	uint8* mapPtr = nullptr;

};
