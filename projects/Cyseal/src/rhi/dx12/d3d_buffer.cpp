#include "d3d_buffer.h"
#include "d3d_device.h"
#include "d3d_util.h"
#include "d3d_into.h"
#include "d3d_render_command.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/gpu_resource_view.h"
#include "core/assertion.h"
#include <algorithm>

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

void D3DIndexBuffer::initialize(uint32 sizeInBytes, EPixelFormat format)
{
	CHECK(format == EPixelFormat::R16_UINT || format == EPixelFormat::R32_UINT);
	auto device = getD3DDevice()->getRawDevice();

	indexFormat = format;
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

void D3DIndexBuffer::setDebugName(const wchar_t* inDebugName)
{
	CHECK(parentPool == nullptr);
	defaultBuffer->SetName(inDebugName);
}

D3D12_GPU_VIRTUAL_ADDRESS D3DIndexBuffer::getGPUVirtualAddress() const
{
	return defaultBuffer->GetGPUVirtualAddress();
}

//////////////////////////////////////////////////////////////////////////
// D3DBuffer

D3DBuffer::~D3DBuffer()
{
	if (uploadBuffer != nullptr)
	{
		uploadBuffer->Unmap(0, nullptr);
	}
}

void D3DBuffer::initialize(const BufferCreateParams& inCreateParams)
{
	Buffer::initialize(inCreateParams);

	auto device = getD3DDevice()->getRawDevice();

	// NOTE: alignment should be 0 or 65536 for buffers.
	
	// default buffer
	{
		D3D12_RESOURCE_FLAGS resourceFlags = into_d3d::bufferResourceFlags(createParams.accessFlags);
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(createParams.sizeInBytes, resourceFlags, createParams.alignment);
		HR(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(defaultBuffer.GetAddressOf())));
	}
	// upload buffer (if requested)
	if (0 != (createParams.accessFlags & EBufferAccessFlags::CPU_WRITE))
	{
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(createParams.sizeInBytes, D3D12_RESOURCE_FLAG_NONE, createParams.alignment);
		HR(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

		CD3DX12_RANGE readRange(0, 0);
		HR(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&uploadMapPtr)));
		CHECK(uploadMapPtr != nullptr);
	}
}

void D3DBuffer::writeToGPU(RenderCommandList* commandList, uint32 numUploads, Buffer::UploadDesc* uploadDescs)
{
	CHECK(0 != (createParams.accessFlags & EBufferAccessFlags::CPU_WRITE));
	for (uint32 i = 0; i < numUploads; ++i)
	{
		CHECK((createParams.alignment == 0) || (uploadDescs[i].destOffsetInBytes % createParams.alignment == 0));
		CHECK(uploadDescs[i].destOffsetInBytes + uploadDescs[i].sizeInBytes < createParams.sizeInBytes);
	}

	ID3D12GraphicsCommandList* cmdList = static_cast<D3DRenderCommandList*>(commandList)->getRaw();

	auto barrierBefore = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &barrierBefore);

	// #todo-renderdevice: Merge buffer copy regions if contiguous.
	// Below code is not tested at all as there is no multi-write case yet.
#if 0
	// Merge contiguous regions
	std::vector<Buffer::UploadDesc> sortedDescs(numUploads);
	for (uint32 i = 0; i < numUploads; ++i)
	{
		sortedDescs[i] = uploadDescs[i];
	}
	std::sort(sortedDescs.begin(), sortedDescs.end(),
		[](const Buffer::UploadDesc& x, const Buffer::UploadDesc& y)
		{
			return x.destOffsetInBytes < y.destOffsetInBytes;
		}
	);
	std::vector<Buffer::UploadDesc> mergedDescs;
	for (uint32 i = 0; i < numUploads; ++i)
	{
		uint32 j = i;
		uint32 mergedSize = uploadDescs[i].sizeInBytes;
		while (j < numUploads - 1 && uploadDescs[j].destOffsetInBytes + uploadDescs[j].sizeInBytes == uploadDescs[j + 1].destOffsetInBytes)
		{
			++j;
			mergedSize += uploadDescs[j].sizeInBytes;
		}
		mergedDescs.push_back(Buffer::UploadDesc{
			.srcData = nullptr,
			.sizeInBytes = mergedSize,
			.destOffsetInBytes = uploadDescs[i].destOffsetInBytes
		});
		i = j + 1;
	}
	for (uint32 i = 0; i < numUploads; ++i)
	{
		const UploadDesc& desc = uploadDescs[i];
		::memcpy_s(uploadMapPtr + desc.destOffsetInBytes, desc.sizeInBytes, desc.srcData, desc.sizeInBytes);
	}
	for (size_t i = 0; i < mergedDescs.size(); ++i)
	{
		cmdList->CopyBufferRegion(
			defaultBuffer.Get(), mergedDescs[i].destOffsetInBytes,
			uploadBuffer.Get(), mergedDescs[i].destOffsetInBytes,
			mergedDescs[i].sizeInBytes);
	}
	uint32 DEBUG_numReduced = numUploads - (uint32)mergedDescs.size();
#endif

	// Naive version
	for (uint32 i = 0; i < numUploads; ++i)
	{
		const UploadDesc& desc = uploadDescs[i];
		::memcpy_s(uploadMapPtr + desc.destOffsetInBytes, desc.sizeInBytes, desc.srcData, desc.sizeInBytes);

		cmdList->CopyBufferRegion(
			defaultBuffer.Get(), desc.destOffsetInBytes,
			uploadBuffer.Get(), desc.destOffsetInBytes,
			desc.sizeInBytes);
	}

	auto barrierAfter = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
	cmdList->ResourceBarrier(1, &barrierAfter);
}
