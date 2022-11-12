#include "d3d_resource.h"
#include "d3d_resource_view.h"
#include "d3d_device.h"
#include "d3d_render_command.h"
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

ConstantBufferView* D3DConstantBuffer::allocateCBV(
	DescriptorHeap* descHeap,
	uint32 sizeInBytes,
	uint32 bufferingCount)
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

void D3DStructuredBuffer::initialize(
	uint32 inNumElements,
	uint32 inStride,
	EBufferAccessFlags inAccessFlags)
{
	numElements = inNumElements;
	stride = inStride;
	accessFlags = inAccessFlags;

	totalBytes = numElements * stride;
	CHECK((numElements > 0) && (stride > 0));

	D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;
	if (0 != (accessFlags & EBufferAccessFlags::UAV))
	{
		resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	// Create a committed resource
	ID3D12Device* device = static_cast<D3DDevice*>(gRenderDevice)->getRawDevice();
	HR(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(totalBytes, resourceFlags),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&rawBuffer)));

	// Upload heap if required
	if (0 != (accessFlags & EBufferAccessFlags::CPU_WRITE))
	{
		HR(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(totalBytes),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(rawUploadBuffer.GetAddressOf())));
	}

	// SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		// Shader4ComponentMapping must be D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING (0x1688) for structured buffers.
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = numElements;
		srvDesc.Buffer.StructureByteStride = stride;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		getD3DDevice()->allocateSRVHandle(srvHeap, srvHandle, srvDescriptorIndex);
		device->CreateShaderResourceView(rawBuffer.Get(), &srvDesc, srvHandle);

		srv = std::make_unique<D3DShaderResourceView>(this);
		srv->setCPUHandle(srvHandle);
	}

	// UAV
	if (0 != (accessFlags & EBufferAccessFlags::UAV))
	{
		// #todo-renderdevice: UAV counter resource, but will it ever be needed?
		// https://www.gamedev.net/forums/topic/711467-understanding-uav-counters/5444474/
		ID3D12Resource* counterResource = NULL;
		uint64 counterOffsetInBytes = 0;

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = numElements;
		uavDesc.Buffer.StructureByteStride = stride;
		uavDesc.Buffer.CounterOffsetInBytes = counterOffsetInBytes;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		getD3DDevice()->allocateUAVHandle(uavHeap, uavHandle, uavDescriptorIndex);
		device->CreateUnorderedAccessView(rawBuffer.Get(), counterResource, &uavDesc, uavHandle);

		uav = std::make_unique<D3DUnorderedAccessView>(this);
		uav->setCPUHandle(uavHandle);
	}
}

void D3DStructuredBuffer::uploadData(
	RenderCommandList* commandList,
	void* data,
	uint32 sizeInBytes,
	uint32 destOffsetInBytes)
{
	CHECK(0 != (accessFlags & EBufferAccessFlags::CPU_WRITE));
	ID3D12GraphicsCommandList* cmdList = static_cast<D3DRenderCommandList*>(commandList)->getRaw();
	
	cmdList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(rawBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST));

	void* mapPtr;
	rawUploadBuffer->Map(0, nullptr, &mapPtr);
	::memcpy_s(mapPtr, sizeInBytes, data, sizeInBytes);
	rawUploadBuffer->Unmap(0, nullptr);

	cmdList->CopyBufferRegion(
		rawBuffer.Get(), destOffsetInBytes,
		rawUploadBuffer.Get(), 0,
		sizeInBytes);

	cmdList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			rawBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_COMMON));
}

ShaderResourceView* D3DStructuredBuffer::getSRV() const
{
	return srv.get();
}

UnorderedAccessView* D3DStructuredBuffer::getUAV() const
{
	CHECK(0 != (accessFlags & EBufferAccessFlags::UAV));
	return uav.get();
}

//////////////////////////////////////////////////////////////////////////
// D3DAccelerationStructure

ShaderResourceView* D3DAccelerationStructure::getSRV() const
{
	return srv.get();
}

void D3DAccelerationStructure::initialize(
	uint64 TLASResultMaxSize, uint64 TLASScratchSize,
	uint64 BLASResultMaxSize, uint64 BLASScratchSize)
{
	ID3D12DeviceLatest* device = getD3DDevice()->getRawDevice();
	
	allocateUAVBuffer(
		(std::max)(TLASScratchSize, BLASScratchSize),
		&scratchResource,
		//D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON,
		L"AccelStruct_ScratchBuffer");

	allocateUAVBuffer(
		TLASResultMaxSize,
		&tlasResource,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		L"AccelStruct_TLAS");

	allocateUAVBuffer(
		BLASResultMaxSize,
		&blasResource,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		L"AccelStruct_BLAS");

	srv = std::make_unique<D3DShaderResourceView>(this);
}

void D3DAccelerationStructure::uploadInstanceDescs(
	const D3D12_RAYTRACING_INSTANCE_DESC& instanceDesc)
{
	allocateUploadBuffer(
		(void*)(&instanceDesc),
		sizeof(instanceDesc),
		&instanceDescBuffer,
		L"AccelStruct_InstanceDesc");
}

void D3DAccelerationStructure::allocateUAVBuffer(
	UINT64 bufferSize,
	ID3D12Resource** ppResource,
	D3D12_RESOURCE_STATES initialResourceState /*= D3D12_RESOURCE_STATE_COMMON*/,
	const wchar_t* resourceName /*= nullptr*/)
{
	ID3D12DeviceLatest* device = getD3DDevice()->getRawDevice();

	auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	HR(device->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		initialResourceState,
		nullptr,
		IID_PPV_ARGS(ppResource)));
	if (resourceName)
	{
		(*ppResource)->SetName(resourceName);
	}
}

void D3DAccelerationStructure::allocateUploadBuffer(
	void* pData,
	UINT64 datasize,
	ID3D12Resource** ppResource,
	const wchar_t* resourceName /*= nullptr*/)
{
	ID3D12DeviceLatest* device = getD3DDevice()->getRawDevice();

	auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(datasize);
	HR(device->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(ppResource)));
	if (resourceName)
	{
		(*ppResource)->SetName(resourceName);
	}
	void* pMappedData;
	(*ppResource)->Map(0, nullptr, &pMappedData);
	memcpy(pMappedData, pData, datasize);
	(*ppResource)->Unmap(0, nullptr);
}
