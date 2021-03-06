#pragma once

#include "render_device.h"
#include "world/camera.h"
#include "world/scene.h"

enum class ERendererType
{
	Forward,
	Deferred,
};

class Renderer
{
public:
	virtual ~Renderer() = default;

	virtual void initialize(RenderDevice* renderDevice) = 0;
	virtual void render(const SceneProxy* scene, const Camera* camera) = 0;
};
