#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "test_rhi_utils.h"
#include "rhi/buffer.h"
#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestBufferUploadBase
	{
	protected:
		void BufferUploadAndReadback()
		{
			RenderDevice* renderDevice = rhi_test::createHeadlessDevice(graphicsAPI);

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

			std::vector<uint32> uploadData(4096, 0);
			const size_t uploadSize = sizeof(uploadData[0]) * uploadData.size();
			for (size_t i = 0; i < uploadData.size(); ++i)
			{
				uploadData[i] = (uint32)i;
			}

			BufferCreateParams bufferParams{
				.sizeInBytes = uploadSize,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::CPU_WRITE | EBufferAccessFlags::CPU_READBACK,
			};
			Buffer* buffer = renderDevice->createBuffer(bufferParams);

			beginRendering();
			Buffer::UploadDesc uploadDesc{
				.srcData           = uploadData.data(),
				.sizeInBytes       = (uint32)uploadSize,
				.destOffsetInBytes = 0,
			};
			buffer->writeToGPU(commandList, 1, &uploadDesc);
			finishRendering();

			beginRendering();
			auto request = buffer->requestReadback(commandList, 0, Buffer::READBACK_SIZE_ALL);
			CHECK(request != nullptr);
			finishRendering();

			Assert::IsTrue(request->bAvailable);
			Assert::IsTrue(request->readbackSize == uploadSize);
			uint32 numFailed = 0;
			uint32* readbackData = reinterpret_cast<uint32*>(request->readbackData);
			for (size_t i = 0; i < uploadData.size(); ++i)
			{
				if (readbackData[i] != uploadData[i]) numFailed += 1;
			}
			Assert::IsTrue(numFailed == 0);

			renderDevice->destroy();
			delete renderDevice;
		}
	};

	TEST_CLASS(TestBufferUploadD3D12), TestBufferUploadBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(BufferUploadAndReadback)
		{
			TestBufferUploadBase::BufferUploadAndReadback();
		}
	};

	TEST_CLASS(TestBufferUploadVulkan), TestBufferUploadBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(BufferUploadAndReadback)
		{
			TestBufferUploadBase::BufferUploadAndReadback();
		}
	};
}
