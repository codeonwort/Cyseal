#pragma once

#include "pipeline_state.h"
#include "gpu_resource_binding.h"
#include "gpu_resource.h"
#include "gpu_resource_view.h"
#include <memory>

class RenderCommandList;
class Material;
class SceneProxy;
class Camera;

class BasePass final
{
public:
	void initialize();
	void renderBasePass(RenderCommandList* commandList, const SceneProxy* scene, const Camera* camera);

private:
	// Bind root parameters for the current root signature
	void bindRootParameters(RenderCommandList* cmdList, uint32 inNumPayloads);

	void updateMaterialCBV(uint32 payloadID, void* payload, uint32 payloadSize);
	void updateMaterialSRV(RenderCommandList* cmdList, uint32 payloadID, Material* material);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;

	std::unique_ptr<ConstantBuffer> constantBufferMemory;
	std::unique_ptr<DescriptorHeap> cbvStagingHeap;
	std::vector<std::unique_ptr<ConstantBufferView>> materialCBVs;
	std::unique_ptr<ConstantBufferView> sceneUniformCBV;

	VertexInputLayout inputLayout;

	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
	// #todo-sampler: Maybe need a volatileSamplerHeap in similar way?

	uint32 numPayloads = 0;
};
