#pragma once

#include "renderer.h"

class BasePass;

class SceneRenderer : public Renderer
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void render(const SceneProxy* scene, const Camera* camera) override;

protected:
	void createRenderPasses();
	void destroyRenderPasses();

	// createRenderPasses() should create these passes.
	BasePass* basePass = nullptr;

private:
	RenderDevice* device = nullptr;
};
