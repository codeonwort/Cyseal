#include "d3d_buffer.h"
#include "d3d_device.h"
#include "d3d_util.h"
#include "d3d_render_command.h"
#include "core/assertion.h"
#include "render/vertex_buffer_pool.h"
#include "render/gpu_resource_view.h"

WRL::ComPtr<ID3D12Resource> createDefaultBuffer(UINT64 byteSize)
{
	auto device = getD3DDevice()->getRawDevice();

	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
	HR( device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
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

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
	HR(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	auto barrierDesc = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_COPY_DEST);
	commandList->ResourceBarrier(1, &barrierDesc);

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

	auto barrierAfterDesc = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ);
	commandList->ResourceBarrier(1, &barrierAfterDesc);
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

	// Create raw view
	{
		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::R32_TYPELESS;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = sizeInBytes / 4;
		srvDesc.buffer.structureByteStride = 0;
		srvDesc.buffer.flags               = EBufferSRVFlags::Raw;

		srv = std::unique_ptr<ShaderResourceView>(gRenderDevice->createSRV(this, srvDesc));
	}
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


ShaderResourceView* D3DVertexBuffer::getByteAddressView() const
{
	CHECK(srv != nullptr);
	return srv.get();
}

void D3DVertexBuffer::setDebugName(const wchar_t* inDebugName)
{
	CHECK(parentPool == nullptr);
	defaultBuffer->SetName(inDebugName);
}

//////////////////////////////////////////////////////////////////////////
// D3DIndexBuffer

void D3DIndexBuffer::initialize(uint32 sizeInBytes, EPixelFormat format)
{
	CHECK(format == EPixelFormat::R16_UINT || format == EPixelFormat::R32_UINT);
	auto device = getD3DDevice()->getRawDevice();

	indexFormat = format;
	defaultBuffer = createDefaultBuffer(sizeInBytes);

	view.BufferLocation = defaultBuffer->GetGPUVirtualAddress();
	view.SizeInBytes = sizeInBytes;
	//view.Format is set in updateData().

	// Create raw view
	if (format != EPixelFormat::UNKNOWN)
	{
		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::R32_TYPELESS;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = sizeInBytes / 4;
		srvDesc.buffer.structureByteStride = 0;
		srvDesc.buffer.flags               = EBufferSRVFlags::Raw;

		srv = std::unique_ptr<ShaderResourceView>(gRenderDevice->createSRV(this, srvDesc));
	}
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
	CHECK(indexFormat == format);

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

ShaderResourceView* D3DIndexBuffer::getByteAddressView() const
{
	CHECK(srv != nullptr);
	return srv.get();
}

void D3DIndexBuffer::setDebugName(const wchar_t* inDebugName)
{
	CHECK(parentPool == nullptr);
	defaultBuffer->SetName(inDebugName);
}

D3D12_GPU_VIRTUAL_ADDRESS D3DIndexBuffer::getGPUVirtualAddress() const
{
	return defaultBuffer->GetGPUVirtualAddress();
}
