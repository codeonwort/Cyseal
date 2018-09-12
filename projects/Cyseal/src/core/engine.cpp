#include "engine.h"
#include "assertion.h"
#include "util/unit_test.h"
#include "render/raw_api/dx12/d3d_device.h"
#include "render/raw_api/vulkan/vk_device.h"
#include "render/forward_renderer.h"

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

	// Rendering
	createRenderDevice(createParams.renderDevice);
	createRenderer(createParams.rendererType);

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
}

void CysealEngine::createRenderer(ERendererType rendererType)
{
	switch (rendererType)
	{
	case ERendererType::Forward:
		renderer = new ForwardRenderer;
		break;

	default:
		// Not implemented yet.
		CHECK_NO_ENTRY();
	}

	renderer->initialize(renderDevice);
}
