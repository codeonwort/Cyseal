#pragma once

#include "scene_render_pass.h"
#include "rhi/rhi_forward.h"

class SceneProxy;
class Camera;

struct DepthPrepassInput
{
	const SceneProxy*      scene;
	const Camera*          camera;
};

// Render scene dpeth.
class DepthPrepass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void renderDepthPrepass(RenderCommandList* commandList, uint32 swapchainIndex, const DepthPrepassInput& passInput);

private:
	//
};
