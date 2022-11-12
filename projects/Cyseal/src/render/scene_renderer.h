#pragma once

#include "renderer.h"

class Texture;
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
	
	void rebuildAccelerationStructure(RenderCommandList* commandList, const SceneProxy* scene);

private:
	RenderDevice* device = nullptr;

	// ------------------------------------------------------------------------
	// #todo-renderer: Temporarily manage render targets in the renderer.
	Texture* RT_sceneColor = nullptr;
	Texture* RT_sceneDepth = nullptr; // Actually DS target but let's unify the prefix

	// Gonna stick to forward shading, but render thin GBuffers like DOOM reboot series.
	Texture* RT_thinGBufferA = nullptr; // #todo-renderer: Maybe switch to R10G10B10A2?

	// #todo-wip-rt: Try specular GI with DXR
	Texture* RT_indirectSpecular = nullptr;

	AccelerationStructure* accelStructure = nullptr;

	// ------------------------------------------------------------------------
	// Render passes
	GPUScene* gpuScene = nullptr;
	BasePass* basePass = nullptr;
	RayTracedReflections* rtReflections = nullptr;
	ToneMapping* toneMapping = nullptr;
};
