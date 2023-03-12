#include "engine.h"
#include "platform.h"
#include "assertion.h"

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
#define INDEX_BUFFER_POOL_SIZE  (16 * 1024 * 1024) // 16 MiB

DEFINE_LOG_CATEGORY(LogEngine);

CysealEngine::~CysealEngine()
{
	CHECK(state == EEngineState::SHUTDOWN);
}

void CysealEngine::startup(const CysealEngineCreateParams& createParams)
{
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
		gDescriptorHeaps = new GlobalDescriptorHeaps;
		gDescriptorHeaps->initialize();

		gVertexBufferPool = new VertexBufferPool;
		gVertexBufferPool->initialize(VERTEX_BUFFER_POOL_SIZE);

		gIndexBufferPool = new IndexBufferPool;
		gIndexBufferPool->initialize(INDEX_BUFFER_POOL_SIZE);

		gTextureManager = new TextureManager;
		gTextureManager->initialize();
	}

	// Rendering
	createRenderer(createParams.rendererType);

	CYLOG(LogEngine, Log, TEXT("Renderer has been initialized."));

	// Dear IMGUI
	createDearImgui(createParams.renderDevice.nativeWindowHandle);
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

	// #todo-imgui: Delegate to Application and RenderDevice
	renderDevice->shutdownDearImgui();
#if PLATFORM_WINDOWS
	ImGui_ImplWin32_Shutdown();
#else
	#error "Not implemented yet"
#endif
	ImGui::DestroyContext();

	// Subsystems
	{
		delete gDescriptorHeaps;
		gDescriptorHeaps = nullptr;

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

	delete renderDevice;
	gRenderDevice = nullptr;

	// Shutdown is finished.
	state = EEngineState::SHUTDOWN;

	CYLOG(LogEngine, Log, TEXT("Engine has been fully terminated."));
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
		renderer = new SceneRenderer;
		break;
	case ERendererType::Null:
		renderer = new NullRenderer;
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
	dearIO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	dearIO.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	ImGui::StyleColorsLight();

	// #todo-imgui: Delegate to Application and RenderDevice
#if PLATFORM_WINDOWS
	ImGui_ImplWin32_Init((HWND*)nativeWindowHandle);
#else
	#error "Not implemented yet"
#endif
}
