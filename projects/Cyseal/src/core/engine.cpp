#include "engine.h"
#include "assertion.h"

#include "util/unit_test.h"
#include "util/resource_finder.h"
#include "util/logging.h"

#include "render/scene_renderer.h"
#include "render/texture_manager.h"

#if COMPILE_BACKEND_DX12
	#include "render/raw_api/dx12/d3d_device.h"
#endif
#if COMPILE_BACKEND_VULKAN
	#include "render/raw_api/vulkan/vk_device.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogEngine);

CysealEngine::CysealEngine()
{
	state = EEngineState::UNINITIALIZED;

	renderDevice = nullptr;
	renderer = nullptr;
}

CysealEngine::~CysealEngine()
{
	CHECK(state == EEngineState::SHUTDOWN);
}

void CysealEngine::startup(const CysealEngineCreateParams& createParams)
{
	CHECK(state == EEngineState::UNINITIALIZED);

	CYLOG(LogEngine, Log, TEXT("Start engine initialization."));

	ResourceFinder::get().add(L"../");
	ResourceFinder::get().add(L"../../");
	ResourceFinder::get().add(L"../../shaders/");
	ResourceFinder::get().add(L"../../resource/");

	// Rendering
	createRenderDevice(createParams.renderDevice);
	createRenderer(createParams.rendererType);

	// Subsystems
	createTextureManager();

	CYLOG(LogEngine, Log, TEXT("Renderer has been initialized."));

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

	// Subsystems
	delete gTextureManager; gTextureManager = nullptr;

	// Rendering
	delete renderer;
	delete renderDevice; gRenderDevice = nullptr;

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
		renderDevice = new D3DDevice;
#endif
		break;

	default:
		CHECK_NO_ENTRY();
	}

	renderDevice->initialize(createParams);

	gRenderDevice = renderDevice;
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

void CysealEngine::createTextureManager()
{
	gTextureManager = new TextureManager;
	gTextureManager->initialize();
}
