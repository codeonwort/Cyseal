#pragma once

#include "rhi/render_device.h"
#include "render/renderer.h"
#include "core/int_types.h"
#include "util/logging.h"

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

	void beginImguiNewFrame();
	void renderImgui();

	inline RenderDevice* getRenderDevice() const { return renderDevice; }
	inline Renderer* getRenderer() const { return renderer; }

private:
	void createRenderDevice(const RenderDeviceCreateParams& createParams);
	void createRenderer(ERendererType rendererType);
	void createDearImgui(void* nativeWindowHandle);

private:
	EEngineState state = EEngineState::UNINITIALIZED;

	RenderDevice* renderDevice = nullptr;
	Renderer* renderer = nullptr;
};
