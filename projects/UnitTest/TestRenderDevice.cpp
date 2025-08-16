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
	class TestRenderDeviceBase
	{
	// Test methods
	protected:
		void CreateAndDestroyHeadlessDevice()
		{
			RenderDevice* renderDevice = createRenderDevice();

			renderDevice->destroy();
			delete renderDevice;
		}

		void CreateBuffer()
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
	};

	TEST_CLASS(TestRenderDeviceD3D12), TestRenderDeviceBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(CreateAndDestroyHeadlessDevice)
		{
			TestRenderDeviceBase::CreateAndDestroyHeadlessDevice();
		}
		TEST_METHOD(CreateBuffer)
		{
			TestRenderDeviceBase::CreateBuffer();
		}
	};

	TEST_CLASS(TestRenderDeviceVulkan), TestRenderDeviceBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(CreateAndDestroyHeadlessDevice)
		{
			TestRenderDeviceBase::CreateAndDestroyHeadlessDevice();
		}
		TEST_METHOD(CreateBuffer)
		{
			TestRenderDeviceBase::CreateBuffer();
		}
	};
}
