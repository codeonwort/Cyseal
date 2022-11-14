#include "engine.h"
#include "assertion.h"

#include "util/unit_test.h"
#include "util/resource_finder.h"

#include "render/null_renderer.h"
#include "render/scene_renderer.h"
#include "render/texture_manager.h"
#include "render/vertex_buffer_pool.h"

#if COMPILE_BACKEND_DX12
	#include "render/raw_api/dx12/d3d_device.h"
#endif
#if COMPILE_BACKEND_VULKAN
	#include "render/raw_api/vulkan/vk_device.h"
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

	ResourceFinder::get().add(L"../");
	ResourceFinder::get().add(L"../../");
	ResourceFinder::get().add(L"../../shaders/");
	ResourceFinder::get().add(L"../../resource/");

	// Core
	createRenderDevice(createParams.renderDevice);

	// Subsystems
	{
		gTextureManager = new TextureManager;
		gTextureManager->initialize();

		gVertexBufferPool = new VertexBufferPool;
		gVertexBufferPool->initialize(VERTEX_BUFFER_POOL_SIZE);

		gIndexBufferPool = new IndexBufferPool;
		gIndexBufferPool->initialize(INDEX_BUFFER_POOL_SIZE);
	}

	// Rendering
	createRenderer(createParams.rendererType);

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
