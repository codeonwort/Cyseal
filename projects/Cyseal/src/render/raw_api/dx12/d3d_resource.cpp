#include "d3d_resource.h"
#include "d3d_device.h"
#include "d3d_resource_view.h"
#include "render/render_device.h"

//////////////////////////////////////////////////////////////////////////
// D3DConstantBuffer

D3DConstantBuffer::~D3DConstantBuffer()
{
	destroy();
}

void D3DConstantBuffer::initialize(uint32 sizeInBytes)
{
	CHECK((sizeInBytes > 0) && (sizeInBytes % (1024 * 64) == 0)); // Multiples of 64 KiB
	
	totalBytes = sizeInBytes;

	// Create a committed resource
	ID3D12Device* device = static_cast<D3DDevice*>(gRenderDevice)->getRawDevice();
	HR(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&memoryPool)));

	// No read from CPU
	CD3DX12_RANGE readRange(0, 0);
	HR(memoryPool->Map(0, &readRange, reinterpret_cast<void**>(&mapPtr)));
	CHECK(mapPtr != nullptr);
}

ConstantBufferView* D3DConstantBuffer::allocateCBV(DescriptorHeap* descHeap, uint32 sizeInBytes, uint32 bufferingCount)
{
	CHECK(bufferingCount >= 1);

	uint32 sizeAligned = (sizeInBytes + 255) & ~255;
	if (allocatedBytes + sizeAligned * bufferingCount >= totalBytes)
	{
		CHECK_NO_ENTRY(); // For now make sure we don't reach here.
		return nullptr;
	}

	D3DDevice* d3dDevice = static_cast<D3DDevice*>(gRenderDevice);
	D3DDescriptorHeap* d3dDescHeap = static_cast<D3DDescriptorHeap*>(descHeap);
	ID3D12Device* rawDevice = d3dDevice->getRawDevice();
	ID3D12DescriptorHeap* rawDescHeap = d3dDescHeap->getRaw();

	D3DConstantBufferView* cbv = new D3DConstantBufferView(this, allocatedBytes, sizeAligned, bufferingCount);
	for (uint32 bufferingIx = 0; bufferingIx < bufferingCount; ++bufferingIx)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
		viewDesc.BufferLocation = memoryPool->GetGPUVirtualAddress() + allocatedBytes;
		viewDesc.SizeInBytes = sizeAligned;

		D3D12_CPU_DESCRIPTOR_HANDLE descHandle = rawDescHeap->GetCPUDescriptorHandleForHeapStart();
		uint32 descIndex = d3dDescHeap->allocateDescriptorIndex();
		descHandle.ptr += descIndex * d3dDevice->getDescriptorSizeCbvSrvUav();

		rawDevice->CreateConstantBufferView(&viewDesc, descHandle);
		
		cbv->initialize(descIndex, bufferingIx);

		allocatedBytes += sizeAligned;
	}

	return cbv;
}

void D3DConstantBuffer::destroy()
{
	if (memoryPool != nullptr)
	{
		CD3DX12_RANGE readRange(0, 0);
		memoryPool->Unmap(0, &readRange);

		mapPtr = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////
// D3DStructuredBuffer

void D3DStructuredBuffer::initialize(uint32 inNumElements, uint32 inStride)
{
	numElements = inNumElements;
	stride = inStride;
	totalBytes = numElements * stride;
	CHECK((numElements > 0) && (stride > 0));

	// Create a committed resource
	ID3D12Device* device = static_cast<D3DDevice*>(gRenderDevice)->getRawDevice();
	HR(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(totalBytes),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&rawBuffer)));

	// SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN; // #todo-wip: unknown right?
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; // #todo-wip: Does it matter?
		srvDesc.Buffer.FirstElement = 0; // #todo-wip: Map to whole range
		srvDesc.Buffer.NumElements = numElements;
		srvDesc.Buffer.StructureByteStride = stride;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		// #todo-wip: Actually it abuses SRV heap for gTextureManager.
		getD3DDevice()->allocateSRVHandle(srvHandle, srvDescriptorIndex);
		device->CreateShaderResourceView(rawBuffer.Get(), &srvDesc, srvHandle);

		srv = std::make_unique<D3DShaderResourceView>(this);
		srv->setCPUHandle(srvHandle);
	}

	// #todo-wip: UAV for StructuredBuffer
	{
		//
	}
}

ShaderResourceView* D3DStructuredBuffer::getSRV() const
{
	return srv.get();
}

UnorderedAccessView* D3DStructuredBuffer::getUAV() const
{
	return uav.get();
}
