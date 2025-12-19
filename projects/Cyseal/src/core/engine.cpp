#include "engine.h"
#include "platform.h"
#include "assertion.h"
#include "memory/custom_new_delete.h"
#include "memory/memory_tracker.h"

#include "util/unit_test.h"
#include "util/resource_finder.h"

#include "rhi/global_descriptor_heaps.h"
#include "rhi/texture_manager.h"
#include "rhi/vertex_buffer_pool.h"
#include "render/null_renderer.h"
#include "render/scene_renderer.h"

#include "imgui.h"

#if PLATFORM_WINDOWS
	#include "imgui_impl_win32.h"
#endif

#if COMPILE_BACKEND_DX12
	#include "rhi/dx12/d3d_device.h"
	#include "imgui_impl_dx12.h"
#endif
#if COMPILE_BACKEND_VULKAN
	#include "rhi/vulkan/vk_device.h"
#endif

#define VERTEX_BUFFER_POOL_SIZE (64 * 1024 * 1024) // 64 MiB
#define INDEX_BUFFER_POOL_SIZE  (64 * 1024 * 1024) // 64 MiB

DEFINE_LOG_CATEGORY(LogEngine);

CysealEngine::~CysealEngine()
{
	CHECK(state == EEngineState::SHUTDOWN);

	MemoryTracker::get().report();
}

void CysealEngine::startup(const CysealEngineCreateParams& inCreateParams)
{
	createParams = inCreateParams;

	CHECK(state == EEngineState::UNINITIALIZED);

	CYLOG(LogEngine, Log, TEXT("Start engine initialization."));

	ResourceFinder::get().addBaseDirectory(L"../");
	ResourceFinder::get().addBaseDirectory(L"../../");
	ResourceFinder::get().addBaseDirectory(L"../../shaders/");
	ResourceFinder::get().addBaseDirectory(L"../../external/");

	// Core
	createRenderDevice(createParams.renderDevice);

	// Subsystems
	{
		gDescriptorHeaps = new(EMemoryTag::RHI) GlobalDescriptorHeaps;
		gDescriptorHeaps->initialize();

		gVertexBufferPool = new(EMemoryTag::RHI) VertexBufferPool;
		gVertexBufferPool->initialize(VERTEX_BUFFER_POOL_SIZE);

		gIndexBufferPool = new(EMemoryTag::RHI) IndexBufferPool;
		gIndexBufferPool->initialize(INDEX_BUFFER_POOL_SIZE);

		gTextureManager = new(EMemoryTag::RHI) TextureManager;
		gTextureManager->initialize();
	}

	// Renderer
	createRenderer(createParams.rendererType);

	CYLOG(LogEngine, Log, TEXT("Renderer has been initialized."));

	// Dear IMGUI
	createDearImgui(createParams.renderDevice.swapChainParams.nativeWindowHandle);
	renderDevice->initializeDearImgui();

	CYLOG(LogEngine, Log, TEXT("Dear IMGUI has been initialized."));

	// Unit test
	UnitTestValidator::runAllUnitTests();

	// Startup is finished.
	state = EEngineState::RUNNING;

	CYLOG(LogEngine, Log, TEXT("Engine has been fully initialized."));
}

void CysealEngine::shutdown()
{
	CHECK(state == EEngineState::RUNNING);

	CYLOG(LogEngine, Log, TEXT("Start engine termination."));

	// Ensure no GPU commands in flight.
	renderDevice->flushCommandQueue();

	renderDevice->shutdownDearImgui();
#if PLATFORM_WINDOWS
	ImGui_ImplWin32_Shutdown();
#else
	#error "Not implemented yet"
#endif
	ImGui::DestroyContext();

	// Subsystems (pre)
	{
		gVertexBufferPool->destroy();
		delete gVertexBufferPool;
		gVertexBufferPool = nullptr;

		gIndexBufferPool->destroy();
		delete gIndexBufferPool;
		gIndexBufferPool = nullptr;

		gTextureManager->destroy();
		delete gTextureManager;
		gTextureManager = nullptr;
	}

	// Rendering
	renderer->destroy();
	delete renderer;

	// Subsystems (post)
	{
		delete gDescriptorHeaps;
		gDescriptorHeaps = nullptr;
	}

	renderDevice->destroy();
	delete renderDevice;
	gRenderDevice = nullptr;

	// Shutdown is finished.
	state = EEngineState::SHUTDOWN;

	CYLOG(LogEngine, Log, TEXT("Engine has been fully terminated."));
}

void CysealEngine::beginImguiNewFrame()
{
	renderDevice->beginDearImguiNewFrame();

#if PLATFORM_WINDOWS
	ImGui_ImplWin32_NewFrame();
#else
	#error "Not implemented yet"
#endif

	ImGui::NewFrame();
}

void CysealEngine::renderImgui()
{
	ImGui::Render();
}

void CysealEngine::renderScene(SceneProxy* sceneProxy, Camera* camera, const RendererOptions& rendererOptions)
{
	renderer->render(sceneProxy, camera, rendererOptions);
}

void CysealEngine::setRenderResolution(uint32 newWidth, uint32 newHeight)
{
	void* hwnd = createParams.renderDevice.swapChainParams.nativeWindowHandle;
	renderDevice->recreateSwapChain(hwnd, newWidth, newHeight);
	renderer->recreateSceneTextures(newWidth, newHeight);
}

void CysealEngine::createRenderDevice(const RenderDeviceCreateParams& createParams)
{
	switch (createParams.rawAPI)
	{
	case ERenderDeviceRawAPI::DirectX12:
		renderDevice = new D3DDevice;
		break;

	case ERenderDeviceRawAPI::Vulkan:
#if COMPILE_BACKEND_VULKAN
		renderDevice = new VulkanDevice;
#else
		CYLOG(LogEngine, Error, L"Vulkan backend is compiled out. Switch to DX12 backend.");
		renderDevice = new D3DDevice;
#endif
		break;

	default:
		CHECK_NO_ENTRY();
	}

	gRenderDevice = renderDevice;
	renderDevice->initialize(createParams);
}

void CysealEngine::createRenderer(ERendererType rendererType)
{
	switch (rendererType)
	{
	case ERendererType::Standard:
		renderer = new(EMemoryTag::Renderer) SceneRenderer;
		break;
	case ERendererType::Null:
		renderer = new(EMemoryTag::Renderer) NullRenderer;
		break;
	default:
		CHECK_NO_ENTRY();
	}

	renderer->initialize(renderDevice);
}

void CysealEngine::createDearImgui(void* nativeWindowHandle)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	
	ImGuiIO& dearIO = ImGui::GetIO();
	dearIO.IniFilename = NULL;
	//dearIO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	dearIO.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // #note-imgui: Allow navigating GUI via X and B buttons on Xbox gamepad.

	//ImGui::StyleColorsDark();
	ImGui::StyleColorsLight();

#if PLATFORM_WINDOWS
	ImGui_ImplWin32_Init((HWND*)nativeWindowHandle);
#else
	#error "Not implemented yet"
#endif
}
