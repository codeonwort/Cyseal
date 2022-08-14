#pragma once

#include "pipeline_state.h"
#include "resource_binding.h"
#include "gpu_resource.h"
#include <memory>

class RenderCommandList;
class Material;
class SceneProxy;
class Camera;

class BasePass final
{
public:
	struct ConstantBufferPayload
	{
		Float4x4 mvpTransform;
		float r, g, b, a;
	};

public:
	void initialize();
	void renderBasePass(RenderCommandList* commandList, const SceneProxy* scene, const Camera* camera);

private:
	// Bind root parameters for the current root signature
	void bindRootParameters(RenderCommandList* cmdList, uint32 inNumPayloads);

	void updateConstantBuffer(uint32 payloadID, void* payload, uint32 payloadSize);
	void updateMaterial(RenderCommandList* cmdList, uint32 payloadID, Material* material);

	inline PipelineState* getPipelineState() const { return pipelineState.get(); }
	inline RootSignature* getRootSignature() const { return rootSignature.get(); }
	inline EPrimitiveTopology getPrimitiveTopology() const { return EPrimitiveTopology::TRIANGLELIST; }

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;
	std::vector<std::unique_ptr<DescriptorHeap>> cbvHeap;
	std::vector<std::unique_ptr<ConstantBuffer>> constantBuffers;
	VertexInputLayout inputLayout;

	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
	// #todo-sampler: Maybe need a volatileSamplerHeap in similar way?

	uint32 numPayloads = 0;
};
