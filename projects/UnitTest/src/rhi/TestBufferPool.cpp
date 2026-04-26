#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "test_rhi_utils.h"
#include "rhi/buffer.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/global_descriptor_heaps.h"

#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestBufferPoolBase
	{
	protected:
		void SuballocateAndRelease()
		{
			RenderDevice* renderDevice = rhi_test::createHeadlessDevice(graphicsAPI);
			gRenderDevice = renderDevice;

			gDescriptorHeaps = new GlobalDescriptorHeaps;
			gDescriptorHeaps->initialize();

			gVertexBufferPool = new VertexBufferPool;
			gVertexBufferPool->initialize(65536);

			gIndexBufferPool = new IndexBufferPool;
			gIndexBufferPool->initialize(65536);

			auto vbuf0 = gVertexBufferPool->suballocate(512);
			auto vbuf1 = gVertexBufferPool->suballocate(1024);
			auto ibuf0 = gIndexBufferPool->suballocate(10240, EPixelFormat::R32_UINT);

			Assert::IsNotNull(vbuf0);
			Assert::IsNotNull(vbuf1);
			Assert::IsNotNull(ibuf0);

			Assert::IsTrue(vbuf0->removeFromPool());
			Assert::IsTrue(vbuf1->removeFromPool());
			Assert::IsTrue(ibuf0->removeFromPool());

			delete vbuf0;
			delete vbuf1;
			delete ibuf0;

			gVertexBufferPool->destroy();
			delete gVertexBufferPool;

			gIndexBufferPool->destroy();
			delete gIndexBufferPool;

			renderDevice->destroy();
			delete renderDevice;
			gRenderDevice = nullptr;
		}
	};

	TEST_CLASS(TestBufferPoolD3D12), TestBufferPoolBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(SuballocateAndRelease)
		{
			TestBufferPoolBase::SuballocateAndRelease();
		}
	};

	TEST_CLASS(TestBufferPoolVulkan), TestBufferPoolBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(SuballocateAndRelease)
		{
			TestBufferPoolBase::SuballocateAndRelease();
		}
	};
}
