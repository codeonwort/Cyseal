#include "engine.h"
#include "assertion.h"

#include "util/unit_test.h"
#include "util/resource_finder.h"
#include "util/logging.h"

#include "render/forward_renderer.h"

#include "render/raw_api/dx12/d3d_device.h"
#include "render/raw_api/vulkan/vk_device.h"

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

	// Rendering
	createRenderDevice(createParams.renderDevice);
	createRenderer(createParams.rendererType);

	CYLOG(LogEngine, Log, TEXT("Renderer has been initialized."));

	// Unit test
	UnitTestValidator::runAllUnitTests();

	CYLOG(LogEngine, Log, TEXT("All unit tests are passed."));

	// Startup is finished.
	state = EEngineState::RUNNING;

	CYLOG(LogEngine, Log, TEXT("Engine has been fully initialized."));
}

void CysealEngine::shutdown()
{
	CHECK(state == EEngineState::RUNNING);

	CYLOG(LogEngine, Log, TEXT("Start engine termination."));

	// Rendering
	delete renderer;
	delete renderDevice;

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
		renderDevice = new VulkanDevice;
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
	case ERendererType::Forward:
		renderer = new ForwardRenderer;
		break;

	default:
		CHECK_NO_ENTRY();
	}

	renderer->initialize(renderDevice);
}
