#pragma once

#include "renderer.h"

// #todo-wip-rt: Why build error if this is absent?
// (error in unique_ptr + ConstantBufferView)
#include "gpu_resource_view.h"

#include <memory>

class Texture;
class StructuredBuffer;
class ConstantBuffer;
class DescriptorHeap;
class ConstantBufferView;
class GPUScene;
class BasePass;
class RayTracedReflections;
class ToneMapping;

class SceneRenderer final : public Renderer
{
public:
	~SceneRenderer() = default;

	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void destroy() override;
	virtual void render(const SceneProxy* scene, const Camera* camera) override;

	virtual void recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight) override;
	
	void updateSceneUniform(
		uint32 swapchainIndex,
		const SceneProxy* scene,
		const Camera* camera);

	void rebuildAccelerationStructure(RenderCommandList* commandList, const SceneProxy* scene);

private:
	RenderDevice* device = nullptr;

	// ------------------------------------------------------------------------
	// #todo-renderer: Temporarily manage render targets in the renderer.
	Texture* RT_sceneColor = nullptr;
	Texture* RT_sceneDepth = nullptr; // Actually DS target but let's unify the prefix

	// Gonna stick to forward shading, but render thin GBuffers like DOOM reboot series.
	Texture* RT_thinGBufferA = nullptr; // #todo-renderer: Maybe switch to R10G10B10A2?

	Texture* RT_indirectSpecular = nullptr;

	// #todo-renderer: Temp dedicated memory and desc heap for scene uniforms
	std::unique_ptr<ConstantBuffer> sceneUniformMemory;
	std::unique_ptr<DescriptorHeap> sceneUniformDescriptorHeap;
	std::unique_ptr<ConstantBufferView> sceneUniformCBV;

	std::unique_ptr<StructuredBuffer> blasTransformBuffer;
	AccelerationStructure* accelStructure = nullptr;

	// ------------------------------------------------------------------------
	// Render passes
	GPUScene* gpuScene = nullptr;
	BasePass* basePass = nullptr;
	RayTracedReflections* rtReflections = nullptr;
	ToneMapping* toneMapping = nullptr;
};
