#pragma once

#include "rhi/render_device.h"
#include "rhi/render_command.h"
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

// #todo-renderer: Currently every custom commands are executed prior to whole internal rendering pipeline.
// Needs a lambda wrapper for each internal command for perfect queueing.
struct EnqueueCustomRenderCommand
{
	EnqueueCustomRenderCommand(RenderCommandList::CustomCommandType inLambda);
};

// Enqueues custom render commands that will be executed at next frame rendering.
// Search for executeCustomCommands() from SceneRenderer or NullRenderer.
// Only works if render device is not headless and renderer is running.
#define ENQUEUE_RENDER_COMMAND(CommandName) EnqueueCustomRenderCommand CommandName

#if 0
// #todo-rendercommand: Resets the list and only executes custom commands registered so far.
// Just a hack due to incomplete render command list support.
struct FlushRenderCommands
{
	FlushRenderCommands();
};

#define FLUSH_RENDER_COMMANDS_INTERNAL(x, y) x ## y
#define FLUSH_RENDER_COMMANDS_INTERNAL2(x, y) FLUSH_RENDER_COMMANDS_INTERNAL(x, y)
#define FLUSH_RENDER_COMMANDS() FlushRenderCommands FLUSH_RENDER_COMMANDS_INTERNAL2(flushRenderCommands_, __LINE__)
#endif

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

	// Enqueue a command that will be executed in the next execution of the renderer.
	void enqueueCustomRenderCommand(RenderCommandList::CustomCommandType inLambda);

private:
	void createRenderDevice(const RenderDeviceCreateParams& createParams);
	void createRenderer(ERendererType rendererType);
	void createDearImgui(void* nativeWindowHandle);

private:
	CysealEngineCreateParams createParams;
	EEngineState state = EEngineState::UNINITIALIZED;

	RenderDevice* renderDevice = nullptr;
	Renderer* renderer = nullptr;

	std::vector<RenderCommandList::CustomCommandType> customRenderCommands;
};

extern CysealEngine* gEngine;
