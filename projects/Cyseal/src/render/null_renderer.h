#pragma once

#include "renderer.h"

// Renders nothing.
class NullRenderer : public Renderer
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void destroy() override;
	virtual void render(const SceneProxy* scene, const Camera* camera) override;

private:
	RenderDevice* device = nullptr;
};
