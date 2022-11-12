#include "d3d_buffer.h"
#include "d3d_device.h"
#include "d3d_util.h"
#include "d3d_render_command.h"
#include "core/assertion.h"
#include "render/vertex_buffer_pool.h"

WRL::ComPtr<ID3D12Resource> createDefaultBuffer(UINT64 byteSize)
{
	auto device = getD3DDevice()->getRawDevice();

	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	HR( device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(defaultBuffer.GetAddressOf())) );

	return defaultBuffer;
}

void updateDefaultBuffer(
	ID3D12GraphicsCommandList* commandList,
	WRL::ComPtr<ID3D12Resource>& defaultBuffer,
	WRL::ComPtr<ID3D12Resource>& uploadBuffer,
	UINT64 defaultBufferOffset,
	const void* initData,
	UINT64 byteSize)
{
	auto device = getD3DDevice()->getRawDevice();

	HR(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	commandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_COPY_DEST));

#if 0
	// This util can't specify default buffer offset :/
	UpdateSubresources<1>(commandList,
		defaultBuffer.Get(), uploadBuffer.Get(),
		0, 0, 1, &subResourceData);
#else
	void* mapPtr;
	uploadBuffer->Map(0, nullptr, &mapPtr);
	memcpy_s(mapPtr, byteSize, initData, byteSize);
	uploadBuffer->Unmap(0, nullptr);

	commandList->CopyBufferRegion(
		defaultBuffer.Get(), defaultBufferOffset,
		uploadBuffer.Get(), 0,
		byteSize);
#endif

	commandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ));
}

//////////////////////////////////////////////////////////////////////////
// D3DVertexBuffer

void D3DVertexBuffer::initialize(uint32 sizeInBytes)
{
	defaultBuffer = createDefaultBuffer(sizeInBytes);
	offsetInDefaultBuffer = 0;

	view.BufferLocation = defaultBuffer->GetGPUVirtualAddress();
	view.SizeInBytes = sizeInBytes;
	//view.StrideInBytes = strideInBytes; // Set in updateData().
}

void D3DVertexBuffer::initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes)
{
	parentPool = pool;
	offsetInDefaultBuffer = offsetInPool;

	D3DVertexBuffer* poolBuffer = static_cast<D3DVertexBuffer*>(pool->internal_getPoolBuffer());
	defaultBuffer = poolBuffer->defaultBuffer;

	view.BufferLocation = defaultBuffer->GetGPUVirtualAddress() + offsetInPool;
	view.SizeInBytes = sizeInBytes;
	//view.StrideInBytes = strideInBytes; // Set in updateData().
}

void D3DVertexBuffer::updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes)
{
	auto cmdList = static_cast<D3DRenderCommandList*>(commandList)->getRaw();

	updateDefaultBuffer(cmdList, defaultBuffer, uploadBuffer,
		offsetInDefaultBuffer, data, view.SizeInBytes);

	view.StrideInBytes  = strideInBytes;

	vertexCount = (uint32)(view.SizeInBytes / strideInBytes);
}


void D3DVertexBuffer::setDebugName(const wchar_t* inDebugName)
{
	CHECK(parentPool == nullptr);
	defaultBuffer->SetName(inDebugName);
}

//////////////////////////////////////////////////////////////////////////
// D3DIndexBuffer

void D3DIndexBuffer::initialize(uint32 sizeInBytes)
{
	defaultBuffer = createDefaultBuffer(sizeInBytes);

	view.BufferLocation = defaultBuffer->GetGPUVirtualAddress();
	view.SizeInBytes = sizeInBytes;
	//view.Format is set in updateData().
}

void D3DIndexBuffer::initializeWithinPool(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes)
{
	parentPool = pool;
	offsetInDefaultBuffer = offsetInPool;

	D3DIndexBuffer* poolBuffer = static_cast<D3DIndexBuffer*>(pool->internal_getPoolBuffer());
	defaultBuffer = poolBuffer->defaultBuffer;

	view.BufferLocation = defaultBuffer->GetGPUVirtualAddress() + offsetInPool;
	view.SizeInBytes = sizeInBytes;
}

void D3DIndexBuffer::updateData(RenderCommandList* commandList, void* data, EPixelFormat format)
{
	indexFormat = format;

	DXGI_FORMAT d3dFormat = DXGI_FORMAT_UNKNOWN;
	uint32 sizeInBytes = view.SizeInBytes;

	switch (format)
	{
	case EPixelFormat::R16_UINT:
		d3dFormat = DXGI_FORMAT_R16_UINT;
		indexCount = sizeInBytes / 2;
		break;
	case EPixelFormat::R32_UINT:
		d3dFormat = DXGI_FORMAT_R32_UINT;
		indexCount = sizeInBytes / 4;
		break;
	}

	CHECK(d3dFormat != DXGI_FORMAT_UNKNOWN);

	auto cmdList = static_cast<D3DRenderCommandList*>(commandList)->getRaw();

	updateDefaultBuffer(cmdList, defaultBuffer, uploadBuffer,
		offsetInDefaultBuffer, data, sizeInBytes);

	view.Format = d3dFormat;
}

void D3DIndexBuffer::setDebugName(const wchar_t* inDebugName)
{
	CHECK(parentPool == nullptr);
	defaultBuffer->SetName(inDebugName);
}
