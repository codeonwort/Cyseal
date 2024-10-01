#include "gpu_resource_view.h"
#include "descriptor_heap.h"

RenderTargetView::~RenderTargetView()
{
	CHECK(sourceHeap->releaseDescriptorIndex(descriptorIndex));
}

DepthStencilView::~DepthStencilView()
{
	CHECK(sourceHeap->releaseDescriptorIndex(descriptorIndex));
}

ShaderResourceView::~ShaderResourceView()
{
	if (!bNoSourceHeap)
	{
		CHECK(sourceHeap->releaseDescriptorIndex(descriptorIndex));
	}
}

UnorderedAccessView::~UnorderedAccessView()
{
	CHECK(sourceHeap->releaseDescriptorIndex(descriptorIndex));
}
