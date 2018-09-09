#include "engine.h"
#include "assertion.h"
#include "util/unit_test.h"
#include "render/raw_api/dx12/d3d_device.h"
#include "render/raw_api/vulkan/vk_device.h"

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

	// Renderer
	createRenderDevice(createParams.renderDevice);

	// Unit test
	UnitTestValidator::runAllUnitTests();

	// Startup is finished.
	state = EEngineState::RUNNING;
}

void CysealEngine::shutdown()
{
	CHECK(state == EEngineState::RUNNING);

	// Renderer
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
