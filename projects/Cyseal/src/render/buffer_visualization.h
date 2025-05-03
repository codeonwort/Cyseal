#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_binding.h"
#include "render/renderer_options.h"

class RenderCommandList;
class ShaderResourceView;

struct BufferVisualizationInput
{
	EBufferVisualizationMode mode           = EBufferVisualizationMode::None;
	ShaderResourceView* gbuffer0SRV         = nullptr;
	ShaderResourceView* gbuffer1SRV         = nullptr;
	ShaderResourceView* sceneColorSRV       = nullptr;
	ShaderResourceView* shadowMaskSRV       = nullptr;
	ShaderResourceView* indirectDiffuseSRV  = nullptr;
	ShaderResourceView* indirectSpecularSRV = nullptr;
	ShaderResourceView* velocityMapSRV      = nullptr;
};

// Visualize intermediate rendering data during frame rendering.
class BufferVisualization final : public SceneRenderPass
{
public:
	void initialize();

	void renderVisualization(RenderCommandList* commandList, uint32 swapchainIndex, const BufferVisualizationInput& passInput);

private:
	UniquePtr<GraphicsPipelineState> pipelineState;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
