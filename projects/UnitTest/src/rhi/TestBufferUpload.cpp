#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "test_rhi_utils.h"
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

			// #wip: Write buffer test.
			// ...

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
