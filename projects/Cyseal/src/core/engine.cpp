#include "engine.h"
#include "assertion.h"

#include "util/unit_test.h"
#include "util/resource_finder.h"

#include "render/raw_api/dx12/d3d_device.h"
#include "render/raw_api/vulkan/vk_device.h"
#include "render/forward_renderer.h"
#include "render/raw_api/dx12/d3d_forward_renderer.h"

CysealEngine::CysealEngine()
{
	state = EEngineState::UNINITIALIZED;

	renderDevice = nullptr;
}

CysealEngine::~CysealEngine()
{
	CHECK(state == EEngineState::SHUTDOWN);
}

void CysealEngine::startup(const CysealEngineCreateParams& createParams)
{
	CHECK(state == EEngineState::UNINITIALIZED);

	ResourceFinder::get().add(L"../");
	ResourceFinder::get().add(L"../../");
	ResourceFinder::get().add(L"../../shaders/");

	// Rendering
	createRenderDevice(createParams.renderDevice);
	createRenderer(createParams.renderDevice.rawAPI, createParams.rendererType);

	// Unit test
	UnitTestValidator::runAllUnitTests();

	// Startup is finished.
	state = EEngineState::RUNNING;
}

void CysealEngine::shutdown()
{
	CHECK(state == EEngineState::RUNNING);

	// Rendering
	delete renderer;
	delete renderDevice;

	// Shutdown is finished.
	state = EEngineState::SHUTDOWN;
}

void CysealEngine::createRenderDevice(const RenderDeviceCreateParams& createParams)
{
	switch (createParams.rawAPI)
	{
	case ERenderDeviceRawAPI::DirectX12:
		renderDevice = new D3DDevice;
		renderDevice->initialize(createParams);
		break;

	case ERenderDeviceRawAPI::Vulkan:
		//renderDevice = new VulkanDevice;
		CHECK_NO_ENTRY();
		break;

	default:
		CHECK_NO_ENTRY();
	}

	gRenderDevice = renderDevice;
}

void CysealEngine::createRenderer(ERenderDeviceRawAPI rawAPI, ERendererType rendererType)
{
	switch (rawAPI)
	{
	case ERenderDeviceRawAPI::DirectX12:
		renderer = createD3DRenderer(rendererType);
		break;

	case ERenderDeviceRawAPI::Vulkan:
		renderer = createVulkanRenderer(rendererType);
		break;

	default:
		CHECK_NO_ENTRY();
	}

	renderer->initialize(renderDevice);
}

Renderer* CysealEngine::createD3DRenderer(ERendererType rendererType)
{
	Renderer* renderer = nullptr;

	switch (rendererType)
	{
	case ERendererType::Forward:
		renderer = new D3DForwardRenderer;
		break;

	default:
		// Not implemented yet.
		CHECK_NO_ENTRY();
	}

	return renderer;
}

Renderer* CysealEngine::createVulkanRenderer(ERendererType rendererType)
{
	// Not implemented yet.
	CHECK_NO_ENTRY();

	return nullptr;
}
