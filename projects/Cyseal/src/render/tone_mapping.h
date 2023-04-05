#pragma once

#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"

class RenderCommandList;
class Texture;

class ToneMapping final
{
public:
	void initialize();

	void renderToneMapping(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		Texture* sceneColor,
		Texture* indirectSpecular);

private:
	UniquePtr<PipelineState> pipelineState;
	UniquePtr<RootSignature> rootSignature;
	VertexInputLayout inputLayout;

	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
