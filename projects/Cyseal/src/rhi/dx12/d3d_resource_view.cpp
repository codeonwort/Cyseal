#include "d3d_resource_view.h"
#include "d3d_resource.h"
#include "d3d_buffer.h"
#include "d3d_into.h"

//////////////////////////////////////////////////////////////////////////
// D3DConstantBufferView

void D3DConstantBufferView::writeToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes)
{
	CHECK(sizeInBytes <= sizeAligned);
	buffer->singleWriteToGPU(commandList, srcData, sizeInBytes, offsetInBuffer);
}

D3D12_GPU_VIRTUAL_ADDRESS D3DConstantBufferView::getGPUVirtualAddress()
{
	return into_d3d::id3d12Resource(buffer)->GetGPUVirtualAddress() + offsetInBuffer;
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
