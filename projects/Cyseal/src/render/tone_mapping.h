#pragma once

#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include <memory>

class RenderCommandList;
class Texture;

class ToneMapping final
{
public:
	ToneMapping();

	void renderToneMapping(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		Texture* sceneColor,
		Texture* indirectSpecular);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;
	VertexInputLayout inputLayout;

	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
};
