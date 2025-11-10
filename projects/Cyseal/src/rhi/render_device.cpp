#include "render_device.h"
#include "gpu_resource_binding.h"
#include "descriptor_heap.h"

#include "denoiser_device.h"

RenderDevice* gRenderDevice = nullptr;

DEFINE_LOG_CATEGORY(LogDevice);

void RenderDevice::initialize(const RenderDeviceCreateParams& inCreateParams)
{
	createParams = inCreateParams;

	initializeDenoiserDevice();

	onInitialize(createParams);
}

void RenderDevice::destroy()
{
	onDestroy();

	denoiserDevice->destroy();
	delete denoiserDevice;
}

void RenderDevice::initializeDearImgui()
{
	imguiSRVHeap = createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::CBV_SRV_UAV,
			.numDescriptors = 1,
			.flags          = EDescriptorHeapFlags::ShaderVisible,
			.nodeMask       = 0,
			.purpose        = EDescriptorHeapPurpose::Volatile, // #wip-heap-purpose
		}
	);
}

void RenderDevice::shutdownDearImgui()
{
	CHECK(imguiSRVHeap != nullptr);
	delete imguiSRVHeap;
	imguiSRVHeap = nullptr;
}

void RenderDevice::initializeDenoiserDevice()
{
	denoiserDevice = new DenoiserDevice;
	denoiserDevice->create();
}
