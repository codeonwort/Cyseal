#pragma once

#include "pipeline_state.h"
#include "resource_binding.h"
#include "gpu_resource.h"
#include <memory>

class RenderCommandList;
class Texture;

class ToneMapping final
{
public:
	ToneMapping();

	void renderToneMapping(
		RenderCommandList* commandList,
		Texture* sceneColor);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;
	VertexInputLayout inputLayout;

	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
};