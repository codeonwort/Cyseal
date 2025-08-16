#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

// #todo-test: Can I define a macro for parameterized test without modifying original headers?
// ...

RenderDevice* renderDeviceFactory(ERenderDeviceRawAPI graphicsAPI)
{
	RenderDevice* device = nullptr;
	switch (graphicsAPI)
	{
		case ERenderDeviceRawAPI::DirectX12: device = new D3DDevice; break;
		case ERenderDeviceRawAPI::Vulkan: device = new VulkanDevice; break;
		default: CHECK_NO_ENTRY();
	}
	return device;
}

namespace UnitTest
{
	TEST_CLASS(TestRenderDevice)
	{
	public:
		TEST_METHOD(CreateAndDestroyHeadlessDevice)
		{
			ERenderDeviceRawAPI graphicsAPIs[] = { ERenderDeviceRawAPI::DirectX12, ERenderDeviceRawAPI::Vulkan };
			for (int i = 0; i < _countof(graphicsAPIs); ++i)
			{
				CreateAndDestroyHeadlessDevice_Parameterized(graphicsAPIs[i]);
			}
		}

	private:
		void CreateAndDestroyHeadlessDevice_Parameterized(ERenderDeviceRawAPI graphicsAPI)
		{
			RenderDeviceCreateParams createParams{
				.nativeWindowHandle  = NULL,
				.bHeadless           = true,
				.rawAPI              = graphicsAPI,
				.raytracingTier      = ERaytracingTier::MaxTier,
				.vrsTier             = EVariableShadingRateTier::MaxTier,
				.meshShaderTier      = EMeshShaderTier::MaxTier,
				.samplerFeedbackTier = ESamplerFeedbackTier::MaxTier,
				.enableDebugLayer    = true,
				.bDoubleBuffering    = false,
				.windowType          = EWindowType::WINDOWED,
				.windowWidth         = 1920,
				.windowHeight        = 1080,
			};

			RenderDevice* renderDevice = renderDeviceFactory(graphicsAPI);
			renderDevice->initialize(createParams);

			renderDevice->destroy();
			delete renderDevice;
		}
	};
}
