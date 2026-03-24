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
	public:
		TestTextureUploadBase()
		{
			renderDevice = rhi_test::createHeadlessDevice(graphicsAPI);
		}
		~TestTextureUploadBase()
		{
			renderDevice->destroy();
			delete renderDevice;
		}

	protected:
		void TextureUploadAndReadback()
		{
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

			auto commandList = getCommandList();

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
		}

		void Texture1DPartialReadback()
		{
			TextureCreateParams texParams = TextureCreateParams::texture1D(
				EPixelFormat::R32_UINT,
				ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::CPU_READBACK,
				256);

			std::vector<uint32> texData = std::vector<uint32>(texParams.width, 0);
			for (size_t i = 0; i < texData.size(); ++i)
			{
				texData[i] = (uint32)(i * 3 + 11);
			}

			auto commandList = getCommandList();

			// 1. Upload texture data.
			beginRendering();
			Texture* texture = renderDevice->createTexture(texParams);
			Assert::IsNotNull(texture, L"Failed to create a Texture");
			texture->uploadData(commandList, texData.data(),
				(uint64)(sizeof(uint32) * texParams.width),
				(uint64)(sizeof(uint32) * texParams.width * texParams.height));
			finishRendering();

			// 2. Readback texture data.
			beginRendering();
			//uint32 subBeginX = 0, subEndX = texParams.width; // [begin, end)
			uint32 subBeginX = 27, subEndX = 213; // [begin, end)
			Texture::ReadbackRegion region{
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
				.offsetX = subBeginX,
				.offsetY = 0,
				.offsetZ = 0,
				.sizeX = subEndX - subBeginX,
				.sizeY = 1,
				.sizeZ = 1,
			};
			SharedPtr<Texture::ReadbackHandle> readbackHandle = texture->requestReadback(commandList, region);
			Assert::IsTrue(readbackHandle != nullptr);
			finishRendering();
			Assert::IsTrue(readbackHandle->bAvailable);

			// 3. Assert.
			uint32* readbackData = reinterpret_cast<uint32*>(readbackHandle->readbackData);
			for (size_t i = subBeginX; i < subEndX; ++i)
			{
				Assert::AreEqual(texData[i], readbackData[i - subBeginX]);
			}

			// 4. Cleanup.
			readbackHandle.reset();
			delete texture;
		}

		void Texture2DPartialReadback()
		{
			TextureCreateParams texParams = TextureCreateParams::texture2D(
				EPixelFormat::R32_UINT,
				ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::CPU_READBACK,
				64, 4);

			std::vector<uint32> texData = std::vector<uint32>(texParams.width * texParams.height, 0);
			for (size_t i = 0; i < texData.size(); ++i)
			{
				texData[i] = (uint32)(i * 3 + 11);
			}

			auto commandList = getCommandList();

			// 1. Upload texture data.
			beginRendering();
			Texture* texture = renderDevice->createTexture(texParams);
			Assert::IsNotNull(texture, L"Failed to create a Texture");
			texture->uploadData(commandList, texData.data(),
				(uint64)(sizeof(uint32) * texParams.width),
				(uint64)(sizeof(uint32) * texParams.width * texParams.height));
			finishRendering();

			// 2. Readback texture data.
			beginRendering();
			//uint32 subBeginX = 0, subEndX = texParams.width; // [begin, end)
			uint32 subBeginX = 27, subEndX = 51; // [begin, end)
			uint32 subBeginY = 1, subEndY = 3; // [begin, end)
			Texture::ReadbackRegion region{
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
				.offsetX = subBeginX,
				.offsetY = subBeginY,
				.offsetZ = 0,
				.sizeX = subEndX - subBeginX,
				.sizeY = subEndY - subBeginY,
				.sizeZ = 1,
			};
			SharedPtr<Texture::ReadbackHandle> readbackHandle = texture->requestReadback(commandList, region);
			Assert::IsTrue(readbackHandle != nullptr);
			finishRendering();
			Assert::IsTrue(readbackHandle->bAvailable);

			// 3. Assert.
			uint32* readbackData = reinterpret_cast<uint32*>(readbackHandle->readbackData);
			uint32* readbackDataCurrRow = readbackData;
			uint32 numFailed = 0;
			for (size_t y = subBeginY; y < subEndY; ++y)
			{
				for (size_t x = subBeginX; x < subEndX; ++x)
				{
					size_t ix = y * texParams.width + x;
					if (texData[ix] != readbackDataCurrRow[x - subBeginX])
					{
						++numFailed;
					}
				}
				// NOTE: Be careful that rows might not be tightly packed.
				readbackDataCurrRow += readbackHandle->rowPitch / sizeof(uint32);
			}
			Assert::AreEqual(0u, numFailed);

			// 4. Cleanup.
			readbackHandle.reset();
			delete texture;
		}

		void Texture3DPartialReadback()
		{
			TextureCreateParams texParams = TextureCreateParams::texture3D(
				EPixelFormat::R32_UINT,
				ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::CPU_READBACK,
				64, 4, 4);

			const uint32 totalPixels = texParams.width * texParams.height * texParams.depth;
			std::vector<uint32> texData = std::vector<uint32>(totalPixels, 0);
			for (size_t i = 0; i < texData.size(); ++i)
			{
				texData[i] = (uint32)i + 1;
			}

			auto commandList = getCommandList();

			// 1. Upload texture data.
			beginRendering();
			Texture* texture = renderDevice->createTexture(texParams);
			Assert::IsNotNull(texture, L"Failed to create a Texture");
			texture->uploadData(commandList, texData.data(),
				(uint64)(sizeof(uint32) * texParams.width),
				(uint64)(sizeof(uint32) * texParams.width * texParams.height));
			finishRendering();

			// 2. Readback texture data.
			beginRendering();
			//uint32 subBeginX = 0, subEndX = texParams.width; // [begin, end)
			uint32 subBeginX = 27, subEndX = 51; // [begin, end)
			uint32 subBeginY = 1, subEndY = 3; // [begin, end)
			uint32 subBeginZ = 1, subEndZ = 3; // [begin, end)
			Texture::ReadbackRegion region{
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
				.offsetX = subBeginX,
				.offsetY = subBeginY,
				.offsetZ = subBeginZ,
				.sizeX = subEndX - subBeginX,
				.sizeY = subEndY - subBeginY,
				.sizeZ = subEndZ - subBeginZ,
			};
			SharedPtr<Texture::ReadbackHandle> readbackHandle = texture->requestReadback(commandList, region);
			Assert::IsTrue(readbackHandle != nullptr);
			finishRendering();
			Assert::IsTrue(readbackHandle->bAvailable);

			// 3. Assert.
			uint32* readbackData = reinterpret_cast<uint32*>(readbackHandle->readbackData);
			uint32* readbackDataCurrRow = readbackData;
			uint32 numFailed = 0;
			for (size_t z = subBeginZ; z < subEndZ; ++z)
			{
				for (size_t y = subBeginY; y < subEndY; ++y)
				{
					for (size_t x = subBeginX; x < subEndX; ++x)
					{
						size_t ix = z * (texParams.width * texParams.height) + y * texParams.width + x;
						if (texData[ix] != readbackDataCurrRow[x - subBeginX])
						{
							++numFailed;
						}
					}
					// NOTE: Be careful that rows might not be tightly packed.
					readbackDataCurrRow += readbackHandle->rowPitch / sizeof(uint32);
				}
			}
			Assert::AreEqual(0u, numFailed);

			// 4. Cleanup.
			readbackHandle.reset();
			delete texture;
		}

	private:
		RenderCommandList* getCommandList()
		{
			return renderDevice->getCommandListForCustomCommand();
		}
		void beginRendering()
		{
			RenderCommandList* commandList = getCommandList();
			RenderCommandAllocator* commandAllocator = renderDevice->getCommandAllocator(0);

			commandList->reset(commandAllocator);
		}
		void finishRendering()
		{
			RenderCommandList* commandList = getCommandList();
			RenderCommandAllocator* commandAllocator = renderDevice->getCommandAllocator(0);
			RenderCommandQueue* commandQueue = renderDevice->getCommandQueue();
			SwapChain* nullSwapChain = nullptr;

			commandList->close();
			commandAllocator->markValid();
			commandQueue->executeCommandList(commandList, nullSwapChain);
			renderDevice->flushCommandQueue();
		}

	private:
		RenderDevice* renderDevice = nullptr;
	};

	TEST_CLASS(TestTextureUploadD3D12), TestTextureUploadBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(TextureUploadAndReadback)
		{
			TestTextureUploadBase::TextureUploadAndReadback();
		}
		TEST_METHOD(Texture1DPartialReadback)
		{
			TestTextureUploadBase::Texture1DPartialReadback();
		}
		TEST_METHOD(Texture2DPartialReadback)
		{
			TestTextureUploadBase::Texture2DPartialReadback();
		}
		TEST_METHOD(Texture3DPartialReadback)
		{
			TestTextureUploadBase::Texture3DPartialReadback();
		}
	};

	TEST_CLASS(TestTextureUploadVulkan), TestTextureUploadBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(TextureUploadAndReadback)
		{
			TestTextureUploadBase::TextureUploadAndReadback();
		}
		TEST_METHOD(Texture1DPartialReadback)
		{
			TestTextureUploadBase::Texture1DPartialReadback();
		}
		TEST_METHOD(Texture2DPartialReadback)
		{
			TestTextureUploadBase::Texture2DPartialReadback();
		}
		TEST_METHOD(Texture3DPartialReadback)
		{
			TestTextureUploadBase::Texture3DPartialReadback();
		}
	};
}
