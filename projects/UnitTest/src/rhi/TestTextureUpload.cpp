#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "test_rhi_utils.h"
#include "rhi/render_command.h"
#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestTextureUploadBase
	{
	protected:
		void TextureUploadAndReadback()
		{
			RenderDevice* renderDevice = rhi_test::createHeadlessDevice(graphicsAPI);

			// width should be at least D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
			// to gracefully reuse texData. (row pitch alignment issue)
			TextureCreateParams texParams[] = {
				TextureCreateParams::texture1D(
					EPixelFormat::R32_UINT,
					ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::CPU_READBACK,
					256),
				TextureCreateParams::texture2D(
					EPixelFormat::R32_UINT,
					ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::CPU_READBACK,
					64, 4),
				TextureCreateParams::texture3D(
					EPixelFormat::R32_UINT,
					ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::CPU_READBACK,
					64, 4, 4),
			};
			uint32 kData[] = { 0xdeadbeef, 0x10203040,0x12345678 };
			std::vector<uint32> texData[] = {
				std::vector<uint32>(texParams[0].width, kData[0]),
				std::vector<uint32>(texParams[1].width * texParams[1].height, kData[1]),
				std::vector<uint32>(texParams[2].width * texParams[2].height * texParams[2].depth, kData[2]),
			};
			const int32 numTextures = _countof(texParams);

			auto commandList = renderDevice->getCommandListForCustomCommand();
			auto commandAllocator = renderDevice->getCommandAllocator(0);
			auto commandQueue = renderDevice->getCommandQueue();
			SwapChain* nullSwapChain = nullptr;

			auto beginRendering = [&]() {
				commandList->reset(commandAllocator);
			};
			auto finishRendering = [&]() {
				commandList->close();
				commandAllocator->markValid();
				commandQueue->executeCommandList(commandList, nullSwapChain);
				renderDevice->flushCommandQueue();
			};

			// 1. Upload texture data.
			beginRendering();
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
				else
				{
					textures[i]->uploadData(commandList, texData[i].data(),
						(uint64)(sizeof(uint32) * texParams[i].width),
						(uint64)(sizeof(uint32) * texParams[i].width * texParams[i].height));
				}
			}
			finishRendering();

			// 2. Readback texture data.
			std::vector<SharedPtr<Texture::ReadbackHandle>> readbackHandles;
			beginRendering();
			for (int32 i = 0; i < numTextures; ++i)
			{
				Texture* tex = textures[i];
				Texture::ReadbackRegion region = Texture::ReadbackRegion::mip0(tex);
				auto handle = tex->requestReadback(commandList, region);
				Assert::IsTrue(handle != nullptr);
				readbackHandles.emplace_back(handle);
			}
			finishRendering();
			for (int32 i = 0; i < numTextures; ++i)
			{
				Assert::IsTrue(readbackHandles[i]->bAvailable);
			}

			// 3. Assert.
			for (int32 i = 0; i < numTextures; ++i)
			{
				uint32* readbackData = reinterpret_cast<uint32*>(readbackHandles[i]->readbackData);
				for (size_t j = 0; j < texData[i].size(); ++j)
				{
					Assert::AreEqual(kData[i], readbackData[j]);
				}
			}

			// 4. Cleanup.
			readbackHandles.clear();
			for (int32 i = 0; i < numTextures; ++i)
			{
				delete textures[i];
			}

			renderDevice->destroy();
			delete renderDevice;
		}
	};

	TEST_CLASS(TestTextureUploadD3D12), TestTextureUploadBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(TextureUploadAndReadback)
		{
			TestTextureUploadBase::TextureUploadAndReadback();
		}
	};

	TEST_CLASS(TestTextureUploadVulkan), TestTextureUploadBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(TextureUploadAndReadback)
		{
			TestTextureUploadBase::TextureUploadAndReadback();
		}
	};
}
