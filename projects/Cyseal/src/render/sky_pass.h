#pragma once

#include "scene_render_pass.h"
#include "renderer_options.h"
#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/rhi_forward.h"
#include "render/util/volatile_descriptor.h"

class SceneProxy;
class Camera;

struct SkyPassInput
{
	ConstantBufferView*    sceneUniformBuffer;
	ShaderResourceView*    skyboxSRV;
};

class SkyPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* device, EPixelFormat sceneColorFormat);

	void renderSky(RenderCommandList* commandList, const FrameInfo& frameInfo, const SkyPassInput& passInput);

private:
	UniquePtr<GraphicsPipelineState> pipelineState;

	VolatileDescriptorHelper volatileDescriptor;
};
