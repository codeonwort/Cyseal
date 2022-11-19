#include "render_device.h"

RenderDevice* gRenderDevice = nullptr;

DEFINE_LOG_CATEGORY(LogDevice);

RenderDevice::RenderDevice()
	: swapChain(nullptr)
{
}

RenderDevice::~RenderDevice()
{
}
