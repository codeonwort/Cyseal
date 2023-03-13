#pragma once

#include "rhi/render_device.h"
#include "world/camera.h"
#include "world/scene.h"

enum class ERendererType
{
	Standard,
	Null,
};

struct RendererOptions
{
	bool bEnableRayTracedReflections = true;
};

class Renderer
{
public:
	virtual ~Renderer() = default;

	virtual void initialize(RenderDevice* renderDevice) = 0;
	virtual void destroy() = 0;
	virtual void render(const SceneProxy* scene, const Camera* camera, const RendererOptions& renderOptions) = 0;

	virtual void recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight) = 0;
};
