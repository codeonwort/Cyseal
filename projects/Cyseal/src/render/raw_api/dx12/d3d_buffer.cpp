#include "d3d_buffer.h"
#include "d3d_device.h"
#include "d3d_util.h"
#include "d3d_render_command.h"
#include "core/assertion.h"

WRL::ComPtr<ID3D12Resource> createDefaultBuffer(
	ID3D12GraphicsCommandList*		commandList,
	const void*						initData,
	UINT64							byteSize,
	WRL::ComPtr<ID3D12Resource>&	uploadBuffer)
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

	HR( device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(uploadBuffer.GetAddressOf())) );

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData      = initData;
	subResourceData.RowPitch   = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	commandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST));

	UpdateSubresources<1>(commandList,
		defaultBuffer.Get(), uploadBuffer.Get(),
		0, 0, 1, &subResourceData);

	commandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ));

	return defaultBuffer;
}

void D3DVertexBuffer::initialize(void* initialData, uint32 sizeInBytes, uint32 strideInBytes)
{
	updateData(initialData, sizeInBytes, strideInBytes);
}

void D3DVertexBuffer::updateData(void* data, uint32 sizeInBytes, uint32 strideInBytes)
{
	auto device = getD3DDevice();
	auto cmdList = static_cast<D3DRenderCommandList*>(device->getCommandList())->getRaw();

	defaultBuffer = createDefaultBuffer(cmdList, data, sizeInBytes, uploadBuffer);

	view.BufferLocation = defaultBuffer->GetGPUVirtualAddress();
	view.SizeInBytes    = sizeInBytes;
	view.StrideInBytes  = strideInBytes;
}

D3D12_VERTEX_BUFFER_VIEW D3DVertexBuffer::getView() const
{
	return view;
}

void D3DIndexBuffer::initialize(void* initialData, uint32 sizeInBytes, EPixelFormat format)
{
	updateData(initialData, sizeInBytes, format);
}

void D3DIndexBuffer::updateData(void* data, uint32 sizeInBytes, EPixelFormat format)
{
	DXGI_FORMAT d3dFormat = DXGI_FORMAT_UNKNOWN;

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

	auto device = getD3DDevice();
	auto cmdList = static_cast<D3DRenderCommandList*>(device->getCommandList())->getRaw();

	defaultBuffer = createDefaultBuffer(cmdList, data, sizeInBytes, uploadBuffer);

	view.BufferLocation = defaultBuffer->GetGPUVirtualAddress();
	view.Format         = d3dFormat;
	view.SizeInBytes    = sizeInBytes;
}

uint32 D3DIndexBuffer::getIndexCount()
{
	return indexCount;
}

D3D12_INDEX_BUFFER_VIEW D3DIndexBuffer::getView() const
{
	return view;
}
