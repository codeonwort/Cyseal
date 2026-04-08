#include "pch.h"
#include "test_rhi_utils.h"

#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

namespace rhi_test
{
	RenderDevice* createHeadlessDevice(ERenderDeviceRawAPI graphicsAPI)
	{
		RenderDeviceCreateParams createParams{
			.swapChainParams     = SwapChainCreateParams::noSwapChain(),
			.rawAPI              = graphicsAPI,
			.raytracingTier      = ERaytracingTier::MaxTier,
			.vrsTier             = EVariableShadingRateTier::MaxTier,
			.meshShaderTier      = EMeshShaderTier::MaxTier,
			.samplerFeedbackTier = ESamplerFeedbackTier::MaxTier,
			.enableDebugLayer    = true,
		};

		RenderDevice* device = nullptr;
		switch (graphicsAPI)
		{
			case ERenderDeviceRawAPI::DirectX12: device = new D3DDevice; break;
			case ERenderDeviceRawAPI::Vulkan: device = new VulkanDevice; break;
			default: CHECK_NO_ENTRY();
		}

		device->initialize(createParams);

		return device;
	}
}
