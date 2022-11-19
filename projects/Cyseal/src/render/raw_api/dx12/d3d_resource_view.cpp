#include "d3d_resource_view.h"
#include "d3d_resource.h"
#include "d3d_buffer.h"
#include "d3d_into.h"

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

D3D12_GPU_VIRTUAL_ADDRESS D3DShaderResourceView::getGPUVirtualAddress() const
{
	return into_d3d::id3d12Resource(ownerResource)->GetGPUVirtualAddress();
}

//////////////////////////////////////////////////////////////////////////
// D3DUnorderedAccessView

D3D12_GPU_VIRTUAL_ADDRESS D3DUnorderedAccessView::getGPUVirtualAddress() const
{
	return into_d3d::id3d12Resource(ownerResource)->GetGPUVirtualAddress();
}
