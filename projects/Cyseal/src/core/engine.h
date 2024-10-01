#pragma once

#include "rhi/render_device.h"
#include "render/renderer.h"
#include "core/int_types.h"
#include "util/logging.h"

class SceneProxy;
class Camera;

DECLARE_LOG_CATEGORY(LogEngine);

enum class EEngineState : uint8
{
	UNINITIALIZED,
	RUNNING,
	SHUTDOWN
};

struct CysealEngineCreateParams
{
	RenderDeviceCreateParams renderDevice;
	ERendererType rendererType;
};

class CysealEngine final
{
public:
	explicit CysealEngine() = default;
	~CysealEngine();

	CysealEngine(const CysealEngine& rhs) = delete;
	CysealEngine& operator=(const CysealEngine& rhs) = delete;

	void startup(const CysealEngineCreateParams& createParams);
	void shutdown();

	// Call if GUI is resized.
	void setRenderResolution(uint32 newWidth, uint32 newHeight);

	void beginImguiNewFrame();
	void renderImgui();

	void renderScene(SceneProxy* sceneProxy, Camera* camera, const RendererOptions& rendererOptions);

private:
	void createRenderDevice(const RenderDeviceCreateParams& createParams);
	void createRenderer(ERendererType rendererType);
	void createDearImgui(void* nativeWindowHandle);

private:
	CysealEngineCreateParams createParams;
	EEngineState state = EEngineState::UNINITIALIZED;

	RenderDevice* renderDevice = nullptr;
	Renderer* renderer = nullptr;
};
