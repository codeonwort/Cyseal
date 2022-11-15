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

	void renderBasePass(
		RenderCommandList* commandList,
		const SceneProxy* scene,
		const Camera* camera,
		ConstantBufferView* sceneUniformBuffer,
		StructuredBuffer* gpuSceneBuffer);

	// #todo-wip-rt: Temp utils
	uint32 getNumMaterialPayloads() const { return tempNumMaterialPayloads; }
	ConstantBufferView* getMaterialCBV(uint32 payloadID) const { return materialCBVs[payloadID].get(); }

private:
	// Bind root parameters for the current root signature
	void bindRootParameters(
		RenderCommandList* cmdList,
		uint32 inNumPayloads,
		ConstantBufferView* sceneUniform,
		StructuredBuffer* gpuSceneBuffer);

	void updateMaterialParameters(
		RenderCommandList* cmdList,
		uint32 totalPayloads,
		uint32 payloadID,
		Material* material);

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;

	std::unique_ptr<ConstantBuffer> constantBufferMemory;
	std::unique_ptr<DescriptorHeap> cbvStagingHeap;
	std::vector<std::unique_ptr<ConstantBufferView>> materialCBVs;

	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
	// #todo-sampler: Maybe need a volatileSamplerHeap in similar way?

	// #todo-wip-rt: Temp utils
	uint32 tempNumMaterialPayloads = 0;
};
