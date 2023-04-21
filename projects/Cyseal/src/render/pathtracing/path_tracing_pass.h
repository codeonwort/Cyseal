#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"

class PathTracingPass final
{
public:
	void initialize();

	bool isAvailable() const;

	void renderPathTracing(
		RenderCommandList* commandList,
		uint32 swapchainIndex);

private:
	//

private:
	//
};
