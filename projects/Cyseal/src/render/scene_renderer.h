#pragma once

#include "renderer.h"

class BasePass;
class Texture;

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

	// Render passes
	BasePass* basePass = nullptr;
};
