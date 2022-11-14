#include "d3d_resource_view.h"
#include "d3d_resource.h"
#include "d3d_buffer.h"

//////////////////////////////////////////////////////////////////////////
// D3DConstantBufferView

void D3DConstantBufferView::upload(void* data, uint32 sizeInBytes, uint32 bufferingIndex)
{
	CHECK(sizeInBytes <= sizeAligned);
	uint8* destPtr = buffer->mapPtr + offsetInBuffer + (sizeAligned * bufferingIndex);
	::memcpy_s(destPtr, sizeInBytes, data, sizeInBytes);
}

D3D12_GPU_VIRTUAL_ADDRESS D3DConstantBufferView::getGPUVirtualAddress()
{
	return buffer->getGPUVirtualAddress() + offsetInBuffer;
}

//////////////////////////////////////////////////////////////////////////
// D3DShaderResourceView

D3D12_GPU_VIRTUAL_ADDRESS D3DShaderResourceView::getGPUVirtualAddress()
{
	if (source == ShaderResourceView::ESource::Texture)
	{
		return static_cast<D3DTexture*>(ownerTexture)->getGPUVirtualAddress();
	}
	else if (source == ShaderResourceView::ESource::StructuredBuffer)
	{
		return static_cast<D3DStructuredBuffer*>(ownerStructuredBuffer)->getGPUVirtualAddress();
	}
	else if (source == ShaderResourceView::ESource::AccelerationStructure)
	{
		return static_cast<D3DAccelerationStructure*>(ownerAccelStruct)->getTLASGpuVirtualAddress();
	}
	else if (source == ShaderResourceView::ESource::IndexBuffer)
	{
		return static_cast<D3DIndexBuffer*>(ownerIndexBuffer)->getGPUVirtualAddress();
	}
	CHECK_NO_ENTRY();
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// D3DUnorderedAccessView

D3D12_GPU_VIRTUAL_ADDRESS D3DUnorderedAccessView::getGPUVirtualAddress()
{
	if (source == UnorderedAccessView::ESource::Texture)
	{
		return static_cast<D3DTexture*>(ownerTexture)->getGPUVirtualAddress();
	}
	else if (source == UnorderedAccessView::ESource::StructuredBuffer)
	{
		return static_cast<D3DStructuredBuffer*>(ownerStructuredBuffer)->getGPUVirtualAddress();
	}
	CHECK_NO_ENTRY();
	return 0;
}
