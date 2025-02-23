#pragma once

#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"

class RenderCommandList;
class ShaderResourceView;

struct ToneMappingInput
{
	ShaderResourceView* sceneColorSRV;
	ShaderResourceView* indirectDiffuseSRV;
	ShaderResourceView* indirectSpecularSRV;
};

class ToneMapping final
{
public:
	void initialize();

	void renderToneMapping(RenderCommandList* commandList, uint32 swapchainIndex, const ToneMappingInput& passInput);

private:
	UniquePtr<GraphicsPipelineState> pipelineState;
	VertexInputLayout inputLayout;

	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
