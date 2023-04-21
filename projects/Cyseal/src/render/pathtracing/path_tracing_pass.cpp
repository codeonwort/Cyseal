#include "path_tracing_pass.h"
#include "rhi/render_device.h"

void PathTracingPass::initialize()
{
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Path Tracing will be disabled.");
		return;
	}

	//
}

bool PathTracingPass::isAvailable() const
{
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}

void PathTracingPass::renderPathTracing(
	RenderCommandList* commandList,
	uint32 swapchainIndex)
{
	//
}
