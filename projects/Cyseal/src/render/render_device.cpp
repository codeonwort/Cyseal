#include "render_device.h"

RenderDevice* gRenderDevice = nullptr;

RenderDevice::RenderDevice()
	: swapChain(nullptr)
{
}

RenderDevice::~RenderDevice()
{
}
