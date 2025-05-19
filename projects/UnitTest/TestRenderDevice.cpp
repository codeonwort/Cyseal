#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "rhi/dx12/d3d_device.h"

namespace UnitTest
{
	TEST_CLASS(TestRenderDevice)
	{
	public:
		TEST_METHOD(CreateAndDestroyHeadlessDevice)
		{
			RenderDeviceCreateParams createParams{
				.nativeWindowHandle  = NULL,
				.bHeadless           = true,
				.rawAPI              = ERenderDeviceRawAPI::DirectX12,
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

			RenderDevice* renderDevice = new D3DDevice;
			renderDevice->initialize(createParams);

			renderDevice->destroy();
			delete renderDevice;
		}
	};
}
