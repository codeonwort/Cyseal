#pragma once

#include "renderer.h"

class BasePass;

class ForwardRenderer : public Renderer
{

public:
	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void render(const SceneProxy* scene, const Camera* camera) override;

protected:
	void createRenderPasses();
	void destroyRenderPasses();

	// createRenderPasses() should create these passes.
	BasePass* basePass;

private:
	RenderDevice* device;

};
