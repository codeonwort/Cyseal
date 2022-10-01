#pragma once

#include "render_device.h"
#include "world/camera.h"
#include "world/scene.h"

enum class ERendererType
{
	Standard,
	Null,
};

class Renderer
{
public:
	virtual ~Renderer() = default;

	virtual void initialize(RenderDevice* renderDevice) = 0;
	virtual void render(const SceneProxy* scene, const Camera* camera) = 0;
};

// Renders nothing.
// #todo-vulkan-fatal: Vulkan backend should not crash when using this.
class NullRenderer : public Renderer
{
public:
	virtual void initialize(RenderDevice*) override {}
	virtual void render(const SceneProxy* scene, const Camera* camera) override {}
};
