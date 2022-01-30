#pragma once

#include "pipeline_state.h"
#include "resource_binding.h"
#include "gpu_resource.h"
#include <memory>

class RenderCommandList;
class Material;

class BasePass
{
public:
	struct ConstantBufferPayload
	{
		Matrix mvpTransform;
		float r, g, b, a;
	};

public:
	void initialize();

	// Bind root parameters for the current root signature
	void bindRootParameters(RenderCommandList* cmdList, uint32 inNumPayloads);

	void updateConstantBuffer(uint32 payloadID, void* payload, uint32 payloadSize);
	void updateMaterial(RenderCommandList* cmdList, uint32 payloadID, Material* material);

	inline PipelineState* getPipelineState() const { return pipelineState.get(); }
	inline RootSignature* getRootSignature() const { return rootSignature.get(); }
	inline EPrimitiveTopology getPrimitiveTopology() const { return EPrimitiveTopology::TRIANGLELIST; }

protected:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;
	std::vector<std::unique_ptr<DescriptorHeap>> cbvHeap;
	std::vector<std::unique_ptr<ConstantBuffer>> constantBuffers;
	VertexInputLayout inputLayout;

	// todo-wip: Descriptors come from many places and I have no confidence
	// I can globally manage all of them in just one heap.
	// Let's copy descriptors that are needed for the current frame here.
	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
	// todo-wip: Maybe need a volatileSamplerHeap in similar way?

	uint32 numPayloads = 0;
};
