#pragma once

#include "rhi/render_device.h"

namespace rhi_test
{
	RenderDevice* createHeadlessDevice(ERenderDeviceRawAPI graphicsAPI);
}
