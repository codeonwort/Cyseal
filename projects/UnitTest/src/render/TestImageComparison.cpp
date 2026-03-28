#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "../rhi/test_rhi_utils.h"

#include "rhi/render_command.h"
#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"
#include "loader/image_loader.h"

#include <filesystem>

// #wip: Move to a util header.
static std::wstring getSolutionDirectory()
{
	static std::wstring solutionDir;
	if (solutionDir.size() == 0)
	{
		std::filesystem::path currentDir = std::filesystem::current_path();
		int count = 64;
		while (count-- > 0)
		{
			auto sln = currentDir;
			sln.append("CysealSolution.sln");
			if (std::filesystem::exists(sln))
			{
				solutionDir = currentDir.wstring() + L"/";
				break;
			}
			currentDir = currentDir.parent_path();
		}
		CHECK(count >= 0); // Couldn't find shader directory
	}
	return solutionDir;
}
static uint32 compareImages(const wchar_t* refImagePath, void* imageActual)
{
	std::wstring solutionDir = getSolutionDirectory();
	if (solutionDir.size() > 0)
	{
		std::wstring fullPath = solutionDir + L"tests/referenceImages/" + refImagePath;

		ImageLoader loader;
		ImageLoadData* refData = loader.load(fullPath, false, false);
		if (refData != nullptr)
		{
			uint8* p1 = reinterpret_cast<uint8*>(refData->buffer);
			uint8* p2 = reinterpret_cast<uint8*>(imageActual);
			int numDiffRows = 0;
			for (uint32 y = 0; y < refData->height; ++y)
			{
				int cmp = std::memcmp(p1, p2, refData->getRowPitch());
				if (0 != cmp) numDiffRows++;
				p1 += refData->getRowPitch();
				p2 += refData->getRowPitch();
			}
			return numDiffRows;
		}
	}
	return 0xffffffff;
}
static std::vector<uint8> rgba32f_to_rgba8ui(float* src, uint32 width, uint32 height)
{
	std::vector<uint8> dst(4 * width * height);
	uint32 p = 0;
	for (uint32 y = 0; y < height; ++y)
	{
		for (uint32 x = 0; x < width; ++x)
		{
			float r = src[p], g = src[p + 1], b = src[p + 2], a = src[p + 3];
			dst[p] = (uint32)(r * 255.0f) & 0xff;
			dst[p + 1] = (uint32)(g * 255.0f) & 0xff;
			dst[p + 2] = (uint32)(b * 255.0f) & 0xff;
			dst[p + 3] = (uint32)(a * 255.0f) & 0xff;
			p += 4;
		}
	}
	return dst;
}
static bool saveActualImage(const wchar_t* filepath, uint8* data, uint32 width, uint32 height)
{
	std::wstring solutionDir = getSolutionDirectory();
	if (solutionDir.size() > 0)
	{
		std::wstring fullPath = solutionDir + L"intermediate/testResults/" + filepath;
		int rowPitch = width * 4;
		bool bRet = ImageLoader::saveAsPng(fullPath, data, width, height, rowPitch);
		return bRet;
	}
	return false;
}

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
			std::vector<uint8> rgba8Data = rgba32f_to_rgba8ui((float*)readbackHandle->readbackData, texParam.width, texParam.height);
			uint32 numDiff = compareImages(L"TestImageComparison/ref.png", rgba8Data.data());
			saveActualImage(L"TestImageComparison/actual.png", rgba8Data.data(), texParam.width, texParam.height);
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
	};

	TEST_CLASS(TestImageComparisonVulkan), TestImageComparisonBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(CompareImages)
		{
			TestImageComparisonBase::CompareImages();
		}
	};
}
