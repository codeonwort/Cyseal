#pragma once

#include "render_pass.h"
#include "pipeline_state.h"
#include "resource_binding.h"
#include <memory>

class BasePass : public RenderPass
{
public:
	void initialize();

	inline PipelineState* getPipelineState() const { return pipelineState.get(); }
	inline RootSignature* getRootSignature() const { return rootSignature.get(); }
	inline EPrimitiveTopology getPrimitiveTopology() const { return EPrimitiveTopology::TRIANGLELIST; }

protected:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;
	VertexInputLayout inputLayout;
};
