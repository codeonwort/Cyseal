#pragma once

#include "renderer.h"

class BasePass;
class Texture;

class SceneRenderer : public Renderer
{
public:
	~SceneRenderer();

	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void render(const SceneProxy* scene, const Camera* camera) override;

protected:
	void createRenderPasses();
	void destroyRenderPasses();

	// createRenderPasses() should create these passes.
	BasePass* basePass = nullptr;

private:
	RenderDevice* device = nullptr;

	// #todo-renderer: Temporarily manage render targets in the renderer.
	Texture* RT_sceneColor = nullptr;
};
