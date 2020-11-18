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
	virtual void upload(void* payload, uint32 payloadSize)
	{
		uint8* mapPtr = nullptr;

		// No read from CPU
		CD3DX12_RANGE readRange(0, 0);
		HR( rawResource->Map(0, &readRange, reinterpret_cast<void**>(&mapPtr)) );
		CHECK(mapPtr != nullptr);

		::memcpy_s(mapPtr, payloadSize, payload, payloadSize);
		
		rawResource->Unmap(0, &readRange);
	}

	void initialize(
		ID3D12Device*         device,
		ID3D12DescriptorHeap* descriptorHeap,
		uint32                heapSize,
		uint32                payloadSize)
	{
		CHECK(heapSize % (1024 * 64) == 0);

		// #todo: It consumes too much memory
		// Create a committed resource
		HR( device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(heapSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&rawResource)) );

		// Create a CBV
		D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
		viewDesc.BufferLocation = rawResource->GetGPUVirtualAddress();
		viewDesc.SizeInBytes = (payloadSize + 255) & ~255;
		
		device->CreateConstantBufferView(
			&viewDesc,
			descriptorHeap->GetCPUDescriptorHandleForHeapStart());
	}

private:
	WRL::ComPtr<ID3D12Resource> rawResource;

};
