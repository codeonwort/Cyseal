#include "render_device.h"
#include "gpu_resource_binding.h"
#include "descriptor_heap.h"

RenderDevice* gRenderDevice = nullptr;

DEFINE_LOG_CATEGORY(LogDevice);

void RenderDevice::initializeDearImgui()
{
	imguiSRVHeap = createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::CBV_SRV_UAV,
			.numDescriptors = 1,
			.flags          = EDescriptorHeapFlags::ShaderVisible,
			.nodeMask       = 0,
		}
	);
}

void RenderDevice::shutdownDearImgui()
{
	CHECK(imguiSRVHeap != nullptr);
	delete imguiSRVHeap;
	imguiSRVHeap = nullptr;
}
