#pragma once

#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"

class RenderCommandList;
class ShaderResourceView;

class ToneMapping final
{
public:
	void initialize();

	void renderToneMapping(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		ShaderResourceView* sceneColorSRV,
		ShaderResourceView* indirectSpecularSRV);

private:
	UniquePtr<GraphicsPipelineState> pipelineState;
	VertexInputLayout inputLayout;

	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
