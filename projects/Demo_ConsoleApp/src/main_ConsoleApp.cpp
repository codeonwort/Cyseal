#include <iostream>

#include "core/console_application.h"
#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

// Demo project to test headless app (console-only, no swapchains).

class DemoConsoleApp : public ConsoleApplication
{
protected:
    virtual void onExecute() override
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

		// #todo-console: Render something.
		// ...

		renderDevice->destroy();
		delete renderDevice;
    }
};

int main()
{
    ApplicationCreateParams createParams{
        .nativeWindowHandle = NULL,
        .applicationName = L"StudyDirectX12",
    };

    DemoConsoleApp app;
    EApplicationReturnCode ret = app.launch(createParams);

    return 0;
}
