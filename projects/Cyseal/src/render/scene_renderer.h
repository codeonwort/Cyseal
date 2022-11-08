#pragma once

#include "renderer.h"

class Texture;
class GPUScene;
class BasePass;
class ToneMapping;

class SceneRenderer final : public Renderer
{
public:
	~SceneRenderer() = default;

	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void destroy() override;
	virtual void render(const SceneProxy* scene, const Camera* camera) override;

private:
	RenderDevice* device = nullptr;

	// #todo-renderer: Temporarily manage render targets in the renderer.
	Texture* RT_sceneColor = nullptr;
	Texture* RT_sceneDepth = nullptr; // Actually DS target but let's unify prefixes

	// Render passes
	GPUScene* gpuScene = nullptr;
	BasePass* basePass = nullptr;
	ToneMapping* toneMapping = nullptr;
};
