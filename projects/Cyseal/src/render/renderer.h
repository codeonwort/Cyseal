#pragma once

#include "rhi/render_device.h"
#include "world/camera.h"
#include "world/scene.h"

enum class ERendererType
{
	Standard,
	Null,
};

enum class EBufferVisualizationMode : uint32
{
	None             = 0,
	DirectLighting   = 1,
	IndirectSpecular = 2,

	Count,
};

inline const char** getBufferVisualizationModeNames()
{
	static const char* strings[] =
	{
		"None",
		"DirectLighting",
		"IndirectSpecular",
	};
	return strings;
};

struct RendererOptions
{
	bool bEnableRayTracedReflections = true;
	bool bEnableIndirectDraw = true;
	bool bEnableGPUCulling = true;
	EBufferVisualizationMode bufferVisualization = EBufferVisualizationMode::None;

	bool bEnablePathTracing = false;
	bool bCameraHasMoved = false;
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
