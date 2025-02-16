#include "ray_traced_shadows.h"

#include "rhi/render_device.h"

void RayTracedShadowsPass::initialize()
{
	if (isAvailable() == false)
	{
		CYLOG(LogDevice, Warning, L"HardwareRT is not available. Ray Traced Shadows will be disabled.");
		return;
	}

	//
}

bool RayTracedShadowsPass::isAvailable() const
{
	return gRenderDevice->getRaytracingTier() != ERaytracingTier::NotSupported;
}
