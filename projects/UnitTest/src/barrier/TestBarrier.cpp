#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"
#include "render/util/volatile_descriptor.h"
#include "util/resource_finder.h"

#define TEST_SHADERS_DIR L"../../projects/UnitTest/src/barrier/"

// #todo-test: Can I define a macro for parameterized test without modifying original headers?
// ...

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestBarrierBase
	{
		struct TestShaders
		{
			UniquePtr<ComputePipelineState> bufferWriteShader;
			UniquePtr<ComputePipelineState> bufferReadShader;
		};

	// Test methods
	protected:
		void ExecuteBufferBarrier()
		{
			// 1. Initialization

			RenderDevice* renderDevice = createRenderDevice();
			auto uavHeap = createDescriptorHeap(renderDevice, EDescriptorHeapType::UAV);
			auto srvHeap = createDescriptorHeap(renderDevice, EDescriptorHeapType::SRV);
			TestShaders shaders = createShaders(renderDevice);

			BufferCreateParams bufferParams{
				.sizeInBytes = 1024 * sizeof(uint32),
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::SRV | EBufferAccessFlags::UAV,
			};
			Buffer* buffer1 = renderDevice->createBuffer(bufferParams);
			Buffer* buffer2 = renderDevice->createBuffer(bufferParams);
			Buffer* buffer3 = renderDevice->createBuffer(bufferParams);
			Assert::IsNotNull(buffer1, L"Buffer is null");
			Assert::IsNotNull(buffer2, L"Buffer is null");
			Assert::IsNotNull(buffer3, L"Buffer is null");

			UnorderedAccessViewDesc uavDesc{
				.format        = EPixelFormat::UNKNOWN,
				.viewDimension = EUAVDimension::Buffer,
				.buffer        = BufferUAVDesc{
					.firstElement         = 0,
					.numElements          = 1024,
					.structureByteStride  = sizeof(uint32),
					.counterOffsetInBytes = 0,
					.flags                = EBufferUAVFlags::None,
				},
			};
			UnorderedAccessView* buffer1UAV = renderDevice->createUAV(buffer1, uavHeap.get(), uavDesc);
			UnorderedAccessView* buffer2UAV = renderDevice->createUAV(buffer2, uavHeap.get(), uavDesc);
			UnorderedAccessView* buffer3UAV = renderDevice->createUAV(buffer3, uavHeap.get(), uavDesc);

			ShaderResourceViewDesc srvDesc{
				.format        = EPixelFormat::UNKNOWN,
				.viewDimension = ESRVDimension::Buffer,
				.buffer        = BufferSRVDesc{
					.firstElement        = 0,
					.numElements         = 1024,
					.structureByteStride = sizeof(uint32),
					.flags               = EBufferSRVFlags::None,
				},
			};
			ShaderResourceView* buffer1SRV = renderDevice->createSRV(buffer1, srvHeap.get(), srvDesc);
			ShaderResourceView* buffer2SRV = renderDevice->createSRV(buffer2, srvHeap.get(), srvDesc);

			VolatileDescriptorHelper writePassDescriptor;
			writePassDescriptor.initialize(renderDevice, L"WriteBufferPass", 1, 0);
			writePassDescriptor.resizeDescriptorHeap(0, 1 * 2);

			// 2. Validation
			auto commandAllocator = renderDevice->getCommandAllocator(0);
			auto commandList = renderDevice->getCommandList(0);
			auto commandQueue = renderDevice->getCommandQueue();

			commandAllocator->reset();
			commandList->reset(commandAllocator);

			// Barrier
			{
				BufferBarrier barriers[] = {
					{
						.syncBefore = EBarrierSync::ALL,
						.syncAfter = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::COMMON,
						.accessAfter = EBarrierAccess::UNORDERED_ACCESS,
						.buffer = buffer1,
					},
					{
						.syncBefore = EBarrierSync::ALL,
						.syncAfter = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::COMMON,
						.accessAfter = EBarrierAccess::UNORDERED_ACCESS,
						.buffer = buffer2,
					},
				};
				commandList->barrier(_countof(barriers), barriers, 0, nullptr, 0, nullptr);
			}
			// Write pass
			{
				auto heap = writePassDescriptor.getDescriptorHeap(0);
				DescriptorIndexTracker tracker;
				for (uint32 i = 0; i < 2; ++i)
				{
					auto uav = (i == 0) ? buffer1UAV : buffer2UAV;

					ShaderParameterTable SPT{};
					SPT.rwBuffer("rwBuffer", uav);

					commandList->setComputePipelineState(shaders.bufferWriteShader.get());
					commandList->bindComputeShaderParameters(shaders.bufferWriteShader.get(), &SPT, heap, &tracker);
					commandList->dispatchCompute(1024, 1, 1);
				}
			}
			// Barrier
			{
				BufferBarrier barriers[] = {
					{
						.syncBefore = EBarrierSync::COMPUTE_SHADING,
						.syncAfter = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::UNORDERED_ACCESS,
						.accessAfter = EBarrierAccess::SHADER_RESOURCE,
						.buffer = buffer1,
					},
					{
						.syncBefore = EBarrierSync::COMPUTE_SHADING,
						.syncAfter = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::UNORDERED_ACCESS,
						.accessAfter = EBarrierAccess::SHADER_RESOURCE,
						.buffer = buffer2,
					},
				};
				commandList->barrier(_countof(barriers), barriers, 0, nullptr, 0, nullptr);
			}

			commandList->close();
			commandAllocator->markValid();

			commandQueue->executeCommandList(commandList);

			renderDevice->flushCommandQueue();

			// 3. Cleanup

			delete buffer1UAV;
			delete buffer2UAV;
			delete buffer3UAV;
			delete buffer1SRV;
			delete buffer2SRV;
			delete buffer1;
			delete buffer2;
			delete buffer3;

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
		UniquePtr<DescriptorHeap> createDescriptorHeap(RenderDevice* device, EDescriptorHeapType type)
		{
			DescriptorHeapDesc desc{
				.type           = type,
				.numDescriptors = 100,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
			};
			DescriptorHeap* heap = device->createDescriptorHeap(desc);
			return UniquePtr<DescriptorHeap>(heap);
		}
		TestShaders createShaders(RenderDevice* device)
		{
			ResourceFinder::get().addBaseDirectory(TEST_SHADERS_DIR);
			TestShaders shaders;
			{
				ShaderStage* cs = device->createShader(EShaderStage::COMPUTE_SHADER, "WriteBufferCS");
				cs->declarePushConstants();
				cs->loadFromFile(L"buffer_test.hlsl", "mainCS", { L"WRITE_PASS" });
				ComputePipelineDesc pipelineDesc{ .cs = cs, .nodeMask = 0 };
				ComputePipelineState* pipeline = device->createComputePipelineState(pipelineDesc);
				CHECK(pipeline != nullptr);
				delete cs;
				shaders.bufferWriteShader = UniquePtr<ComputePipelineState>(pipeline);
			}
			{
				ShaderStage* cs = device->createShader(EShaderStage::COMPUTE_SHADER, "ReadBufferCS");
				cs->declarePushConstants();
				cs->loadFromFile(L"buffer_test.hlsl", "mainCS", { L"READ_PASS" });
				ComputePipelineDesc pipelineDesc{ .cs = cs, .nodeMask = 0 };
				ComputePipelineState* pipeline = device->createComputePipelineState(pipelineDesc);
				CHECK(pipeline != nullptr);
				delete cs;
				shaders.bufferReadShader = UniquePtr<ComputePipelineState>(pipeline);
			}
			return shaders;
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
