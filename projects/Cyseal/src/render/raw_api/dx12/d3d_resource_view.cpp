#include "d3d_resource_view.h"
#include "d3d_resource.h"

//////////////////////////////////////////////////////////////////////////
// D3DConstantBufferView

void D3DConstantBufferView::upload(void* data, uint32 sizeInBytes, uint32 bufferingIndex)
{
	CHECK(sizeInBytes <= sizeAligned);
	uint8* destPtr = buffer->mapPtr + offsetInBuffer + (sizeAligned * bufferingIndex);
	::memcpy_s(destPtr, sizeInBytes, data, sizeInBytes);
}
