#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

// #todo-test: Can I define a macro for parameterized test without modifying original headers?
// ...

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestBarrierBase
	{
	// Test methods
	protected:
		void ExecuteBufferBarrier()
		{
			RenderDevice* renderDevice = createRenderDevice();

			BufferCreateParams bufferParams{
				.sizeInBytes = 65536,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::CBV,
			};
			Buffer* buffer = renderDevice->createBuffer(bufferParams);
			Assert::IsNotNull(buffer, L"Buffer is null");

			delete buffer;
			buffer = nullptr;

			renderDevice->destroy();
			delete renderDevice;
		}

	// Utils
	protected:
		RenderDevice* createRenderDevice()
		{
			RenderDeviceCreateParams createParams{
				.swapChainParams     = SwapChainCreateParams::noSwapChain(),
				.rawAPI              = graphicsAPI,
				.raytracingTier      = ERaytracingTier::MaxTier,
				.vrsTier             = EVariableShadingRateTier::MaxTier,
				.meshShaderTier      = EMeshShaderTier::MaxTier,
				.samplerFeedbackTier = ESamplerFeedbackTier::MaxTier,
				.enableDebugLayer    = true,
				.bDoubleBuffering    = false,
			};

			RenderDevice* device = nullptr;
			switch (graphicsAPI)
			{
				case ERenderDeviceRawAPI::DirectX12: device = new D3DDevice; break;
				case ERenderDeviceRawAPI::Vulkan: device = new VulkanDevice; break;
				default: CHECK_NO_ENTRY();
			}

			device->initialize(createParams);
			CHECK(device->supportsEnhancedBarrier());

			return device;
		}
	};

	TEST_CLASS(TestBarrierD3D12), TestBarrierBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(ExecuteBufferBarrier)
		{
			TestBarrierBase::ExecuteBufferBarrier();
		}
	};

	// #wip: Enable test for vulkan
#if 0
	TEST_CLASS(TestBarrierVulkan), TestBarrierBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(ExecuteBufferBarrier)
		{
			TestBarrierBase::ExecuteBufferBarrier();
		}
	};
#endif
}
