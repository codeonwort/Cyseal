#pragma once

#include "core/int_types.h"

// Naming of 'frameId' and 'frameIndex' is a little vague, but my criteria:
// - Each ID has a unique value. frameID does.
// - An index is, well, an index. frameIndex could be 0 and then again 0 in the next frame.

struct FrameInfo
{
	// Increments whenever renderer is executed.
	uint32 frameID;

	// Equals to (frameID % gRenderDevice->maxFramesInFlight()).
	// - Note that maxFramesInFlight() is 2 if swapchain exists, but 1 if not.
	// - So a render pass that relies on temporal accumulations need to create
	//   e.g., 2 textures regardless of maxFramesInFlight() and index them with (frameID % 2).
	uint32 frameIndex;
};

// Has nothing to do with D3D render pass or vulkan render pass.
class SceneRenderPass
{
public:
	virtual ~SceneRenderPass() {}
};
