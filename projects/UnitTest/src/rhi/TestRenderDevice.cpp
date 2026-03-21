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

		void CreateTexture()
		{
			RenderDevice* renderDevice = createRenderDevice();

			TextureCreateParams texParams[] = {
				TextureCreateParams{
					.dimension = ETextureDimension::TEXTURE1D,
					.format = EPixelFormat::R32_FLOAT,
					.accessFlags = ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::UAV,
					.width = 1024, .height = 1, .depth = 1, .mipLevels = 1,
					.sampleCount = 1, .sampleQuality = 0, .numLayers = 1,
				},
				TextureCreateParams{
					.dimension = ETextureDimension::TEXTURE2D,
					.format = EPixelFormat::R16G16B16A16_FLOAT,
					.accessFlags = ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::RTV | ETextureAccessFlags::SRV,
					.width = 1024, .height = 1024, .depth = 1, .mipLevels = 1,
					.sampleCount = 1, .sampleQuality = 0, .numLayers = 1,
				},
				TextureCreateParams{
					.dimension = ETextureDimension::TEXTURE3D,
					.format = EPixelFormat::R8G8B8A8_UNORM,
					.accessFlags = ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::SRV,
					.width = 64, .height = 64, .depth = 64, .mipLevels = 1,
					.sampleCount = 1, .sampleQuality = 0, .numLayers = 1,
				},
			};
			const int32 numTextures = _countof(texParams);

			Texture* textures[numTextures];
			for (int32 i = 0; i < numTextures; ++i)
			{
				textures[i] = renderDevice->createTexture(texParams[i]);

				if (textures[i] == nullptr)
				{
					wchar_t msg[256];
					std::swprintf(msg, _countof(msg), L"Texture %i is null", i);
					Assert::IsNotNull(textures[i], msg);
				}
			}

			for (int32 i = 0; i < numTextures; ++i)
			{
				delete textures[i];
			}

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
		TEST_METHOD(CreateTexture)
		{
			TestRenderDeviceBase::CreateTexture();
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
		TEST_METHOD(CreateTexture)
		{
			TestRenderDeviceBase::CreateTexture();
		}
	};
}
