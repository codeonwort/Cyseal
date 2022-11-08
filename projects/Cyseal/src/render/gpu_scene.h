#pragma once

#include <memory>

class PipelineState;
class RootSignature;
class StructuredBuffer;
class RenderCommandList;
class SceneProxy;
class Camera;

class GPUScene final
{
public:
	void initialize();
	void renderGPUScene(RenderCommandList* commandList, const SceneProxy* scene, const Camera* camera);

	StructuredBuffer* getGPUSceneBuffer() const;
	StructuredBuffer* getCulledGPUSceneBuffer() const;

private:
	std::unique_ptr<PipelineState> pipelineState;
	std::unique_ptr<RootSignature> rootSignature;

	std::unique_ptr<StructuredBuffer> gpuSceneBuffer;
	std::unique_ptr<StructuredBuffer> culledGpuSceneBuffer;
};
