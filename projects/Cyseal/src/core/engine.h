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

	/// <summary>
	/// Change the internal render resolution of SceneRenderer. The scene rendering result is scaled to the final target (a Texture or a swapchain image).
	/// It's independent of 'swapchain resolution' for window display. Use setDisplayResolution() to change it.
	/// If you want to render at native resolution, you need to invoke both setRenderResolution() and setDisplayResolution() with same arguments.
	/// </summary>
	/// <param name="newWidth"></param>
	/// <param name="newHeight"></param>
	void setRenderResolution(uint32 newWidth, uint32 newHeight);

	/// <summary>
	/// Change the swapchain resolution. Must ensure that no GPU works in flight when calling this method.
	/// The swapchain resolution is independent of 'render resolution' used in SceneRenderer. You need to set it via setRenderResolution().
	/// If you want to render at native resolution, you need to invoke both setRenderResolution() and setDisplayResolution() with same arguments.
	/// </summary>
	/// <param name="newWidth"></param>
	/// <param name="newHeight"></param>
	/// <returns>True if successful. False if RenderDevice is headless (= no swapchain).</returns>
	bool setDisplayResolution(uint32 newWidth, uint32 newHeight);

	/// <summary>
	/// Convenient method to set both resolutions. Equivalent to calling setRenderResolution() and setDisplayResolution().
	/// Regardless of the return value, the setRenderResolution() part is executed.
	/// </summary>
	/// <param name="newWidth"></param>
	/// <param name="newHeight"></param>
	/// <returns>Same as that of setDisplayResolution().</returns>
	bool setRenderAndDisplayResolution(uint32 newWidth, uint32 newHeight);

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
