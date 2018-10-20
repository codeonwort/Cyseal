#include "render_device.h"

RenderDevice* gRenderDevice = nullptr;

RenderDevice::RenderDevice()
	: swapChain(nullptr)
	, commandAllocator(nullptr)
{
}

RenderDevice::~RenderDevice()
{
}
