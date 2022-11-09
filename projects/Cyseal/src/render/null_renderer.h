#pragma once

#include "renderer.h"

// Renders nothing.
class NullRenderer : public Renderer
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void destroy() override;
	virtual void render(const SceneProxy* scene, const Camera* camera) override;

	virtual void recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight) override {}

private:
	RenderDevice* device = nullptr;
};
