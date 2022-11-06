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
	D3DDescriptorHeap(const DescriptorHeapDesc& desc)
		: DescriptorHeap(desc)
	{}

	virtual void setDebugName(const wchar_t* name)
	{
		rawState->SetName(name);
	}

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
	friend class D3DConstantBufferView;

public:
	~D3DConstantBuffer();

	virtual void initialize(uint32 sizeInBytes);

	virtual ConstantBufferView* allocateCBV(DescriptorHeap* descHeap, uint32 sizeInBytes, uint32 bufferingCount);

	void destroy();

private:
	WRL::ComPtr<ID3D12Resource> memoryPool;
	uint32 totalBytes = 0;
	uint32 allocatedBytes = 0;
	uint8* mapPtr = nullptr;
};
