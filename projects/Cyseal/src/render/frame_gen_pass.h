#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "util/volatile_descriptor.h"

struct FrameGenPassInput
{
	//
};

class FrameGenPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput);

private:
	void initializeFSR3();

private:
	RenderDevice* device = nullptr;
};
