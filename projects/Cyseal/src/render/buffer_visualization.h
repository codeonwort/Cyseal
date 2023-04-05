#pragma once

#include "core/smart_pointer.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_binding.h"
#include "render/renderer.h"

class RenderCommandList;
class Texture;

struct BufferVisualizationSources
{
	EBufferVisualizationMode mode = EBufferVisualizationMode::None;

	Texture* sceneColor           = nullptr;
	Texture* indirectSpecular     = nullptr;
};

// Visualize intermediate rendering data during frame rendering.
class BufferVisualization final
{
public:
	void initialize();

	void renderVisualization(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		const BufferVisualizationSources& sources);

private:
	UniquePtr<PipelineState> pipelineState;
	UniquePtr<RootSignature> rootSignature;

	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
