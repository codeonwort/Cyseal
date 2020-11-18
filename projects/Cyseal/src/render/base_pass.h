#pragma once

#include "render_pass.h"
#include "pipeline_state.h"
#include "resource_binding.h"
#include "gpu_resource.h"
#include <memory>

class RenderCommandList;

class BasePass : public RenderPass
{
public:
	struct ConstantBufferPayload
	{
		float r, g, b, a;
	};

public:
	void initialize();

	// #todo-wip: for test
	void bindRootParameter(RenderCommandList* cmdList);
	void updateConstantBuffer(void* payload, uint32 payloadSize);

	inline PipelineState* getPipelineState() const { return pipelineState.get(); }
	inline RootSignature* getRootSignature() const { return rootSignature.get(); }
	inline EPrimitiveTopology getPrimitiveTopology() const { return EPrimitiveTopology::TRIANGLELIST; }

protected:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;
	std::vector<std::unique_ptr<DescriptorHeap>> cbvHeap;
	std::vector<std::unique_ptr<ConstantBuffer>> constantBuffers;
	VertexInputLayout inputLayout;
};
