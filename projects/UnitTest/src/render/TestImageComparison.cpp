#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "test_render_utils.h"
#include "../rhi/test_rhi_utils.h"

#include "rhi/render_command.h"
#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestImageComparisonBase
	{
	public:
		TestImageComparisonBase()
		{
			renderDevice = rhi_test::createHeadlessDevice(graphicsAPI);
		}
		~TestImageComparisonBase()
		{
			renderDevice->destroy();
			delete renderDevice;
		}

	protected:
		void CompareImages()
		{
			TextureCreateParams texParam = TextureCreateParams::texture2D(
				EPixelFormat::R32G32B32A32_FLOAT,
				ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::CPU_READBACK,
				64, 64);

			std::vector<float> texData(4 * texParam.width * texParam.height);
			uint32 p = 0;
			for (uint32 y = 0; y < texParam.height; ++y)
			{
				for (uint32 x = 0; x < texParam.width; ++x)
				{
					float r = (float)((x ^ y) % 255) / 255.0f;
					float g = (float)(((x * 2) ^ (y * 2)) % 255) / 255.0f;
					float b = (float)(((x * 4) ^ (y * 4)) % 255) / 255.0f;
					float a = 1.0f;
					texData[p++] = r; texData[p++] = g; texData[p++] = b; texData[p++] = a;
				}
			}

			auto commandList = getCommandList();

			// 1. Upload texture data.
			beginRendering();
			Texture* texture = renderDevice->createTexture(texParam);
			Assert::IsNotNull(texture);
			texture->uploadData(commandList, texData.data(),
				(uint64)(sizeof(float) * 4 * texParam.width),
				(uint64)(sizeof(float) * 4 * texParam.width * texParam.height));
			finishRendering();

			// 2. Readback texture data.
			beginRendering();
			Texture::ReadbackRegion region = Texture::ReadbackRegion::mip0(texture);
			SharedPtr<Texture::ReadbackHandle> readbackHandle = texture->requestReadback(commandList, region);
			Assert::IsTrue(readbackHandle != nullptr);
			finishRendering();
			Assert::IsTrue(readbackHandle->bAvailable);

			// 3. Assert.
			auto actualImage = render_test::rgba32f_to_rgba8ui((float*)readbackHandle->readbackData, texParam.width * texParam.height);
			uint32 numDiff = render_test::compareRefImageToRgba8ui(L"TestImageComparison/ref.png", actualImage.data());
			render_test::saveRgba8uiImage(L"TestImageComparison/actual.png", actualImage.data(), texParam.width, texParam.height);
			Assert::AreEqual(0u, numDiff);

			// 4. Cleanup.
			readbackHandle.reset();
			delete texture;
		}

		void CompareImagesRgba8()
		{
			TextureCreateParams texParam = TextureCreateParams::texture2D(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::CPU_WRITE | ETextureAccessFlags::CPU_READBACK,
				64, 64);

			std::vector<uint8> texData(4 * texParam.width * texParam.height);
			uint32 p = 0;
			for (uint32 y = 0; y < texParam.height; ++y)
			{
				for (uint32 x = 0; x < texParam.width; ++x)
				{
					uint8 r = ((x * 4) ^ (y * 4)) % 255;
					uint8 g = ((x * 2) ^ (y * 2)) % 255;
					uint8 b = (x ^ y) % 255;
					uint8 a = 255;
					texData[p++] = r; texData[p++] = g; texData[p++] = b; texData[p++] = a;
				}
			}

			auto commandList = getCommandList();

			// 1. Upload texture data.
			beginRendering();
			Texture* texture = renderDevice->createTexture(texParam);
			Assert::IsNotNull(texture);
			texture->uploadData(commandList, texData.data(),
				(uint64)(sizeof(uint8) * 4 * texParam.width),
				(uint64)(sizeof(uint8) * 4 * texParam.width * texParam.height));
			finishRendering();

			// 2. Readback texture data.
			beginRendering();
			Texture::ReadbackRegion region = Texture::ReadbackRegion::mip0(texture);
			SharedPtr<Texture::ReadbackHandle> readbackHandle = texture->requestReadback(commandList, region);
			Assert::IsTrue(readbackHandle != nullptr);
			finishRendering();
			Assert::IsTrue(readbackHandle->bAvailable);

			// 3. Assert.
			auto actualImage = reinterpret_cast<uint8*>(readbackHandle->readbackData);
			uint32 numDiff = render_test::compareRefImageToRgba8ui(L"TestImageComparison/refRgba8.png", actualImage);
			render_test::saveRgba8uiImage(L"TestImageComparison/actualRgba8.png", actualImage, texParam.width, texParam.height);
			Assert::AreEqual(0u, numDiff);

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

	TEST_CLASS(TestImageComparisonD3D12), TestImageComparisonBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(CompareImages)
		{
			TestImageComparisonBase::CompareImages();
		}
		TEST_METHOD(CompareImagesRgba8)
		{
			TestImageComparisonBase::CompareImagesRgba8();
		}
	};

	TEST_CLASS(TestImageComparisonVulkan), TestImageComparisonBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(CompareImages)
		{
			TestImageComparisonBase::CompareImages();
		}
		TEST_METHOD(CompareImagesRgba8)
		{
			TestImageComparisonBase::CompareImagesRgba8();
		}
	};
}
