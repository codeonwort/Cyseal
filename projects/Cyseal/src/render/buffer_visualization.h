#pragma once

#include "scene_render_pass.h"
#include "render/renderer_options.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_binding.h"
#include "util/volatile_descriptor.h"
#include "core/smart_pointer.h"

class RenderCommandList;
class ShaderResourceView;

struct BufferVisualizationInput
{
	Texture*                 renderTarget        = nullptr;
	EBufferVisualizationMode mode                = EBufferVisualizationMode::None;
	uint32                   textureWidth        = 0;
	uint32                   textureHeight       = 0;
	ShaderResourceView*      gbuffer0SRV         = nullptr;
	ShaderResourceView*      gbuffer1SRV         = nullptr;
	ShaderResourceView*      sceneColorSRV       = nullptr;
	ShaderResourceView*      shadowMaskSRV       = nullptr;
	ShaderResourceView*      indirectDiffuseSRV  = nullptr;
	ShaderResourceView*      indirectSpecularSRV = nullptr;
	ShaderResourceView*      velocityMapSRV      = nullptr;
	ShaderResourceView*      visibilityBufferSRV = nullptr;
	ShaderResourceView*      barycentricCoordSRV = nullptr;
	ShaderResourceView*      visGbuffer0SRV      = nullptr;
	ShaderResourceView*      visGbuffer1SRV      = nullptr;
};

// Visualize intermediate rendering data during frame rendering.
class BufferVisualization final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void renderVisualization(RenderCommandList* commandList, uint32 swapchainIndex, const BufferVisualizationInput& passInput);

private:
	GraphicsPipelineState* getPipelineState(Texture* renderTarget) const;

	std::vector<EPixelFormat>                rtvFormats;
	BufferedUniquePtr<GraphicsPipelineState> pipelineStates;

	VolatileDescriptorHelper                 passDescriptor;
};
