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
			auto uavHeap = createDescriptorHeap(renderDevice, EDescriptorHeapType::UAV, 100);
			auto srvHeap = createDescriptorHeap(renderDevice, EDescriptorHeapType::SRV, 100);
			TestShaders shaders = createShaders(renderDevice);

			BufferCreateParams bufferParams{
				.sizeInBytes = 1024 * sizeof(uint32),
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::SRV | EBufferAccessFlags::UAV,
			};
			auto buffer1 = UniquePtr<Buffer>(renderDevice->createBuffer(bufferParams));
			auto buffer2 = UniquePtr<Buffer>(renderDevice->createBuffer(bufferParams));
			auto buffer3 = UniquePtr<Buffer>(renderDevice->createBuffer(bufferParams));
			Assert::IsTrue(buffer1 != nullptr, L"Buffer is null");
			Assert::IsTrue(buffer2 != nullptr, L"Buffer is null");
			Assert::IsTrue(buffer3 != nullptr, L"Buffer is null");

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
			auto buffer1UAV = UniquePtr<UnorderedAccessView>(renderDevice->createUAV(buffer1.get(), uavHeap.get(), uavDesc));
			auto buffer2UAV = UniquePtr<UnorderedAccessView>(renderDevice->createUAV(buffer2.get(), uavHeap.get(), uavDesc));
			auto buffer3UAV = UniquePtr<UnorderedAccessView>(renderDevice->createUAV(buffer3.get(), uavHeap.get(), uavDesc));

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
			auto buffer1SRV = UniquePtr<ShaderResourceView>(renderDevice->createSRV(buffer1.get(), srvHeap.get(), srvDesc));
			auto buffer2SRV = UniquePtr<ShaderResourceView>(renderDevice->createSRV(buffer2.get(), srvHeap.get(), srvDesc));

			VolatileDescriptorHelper writePassDescriptor;
			writePassDescriptor.initialize(renderDevice, L"WriteBufferPass", 1, 0);
			writePassDescriptor.resizeDescriptorHeap(0, 1 * 2);

			VolatileDescriptorHelper readPassDescriptor;
			readPassDescriptor.initialize(renderDevice, L"ReadBufferPass", 1, 0);
			readPassDescriptor.resizeDescriptorHeap(0, 3);

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
						.buffer = buffer1.get(),
					},
					{
						.syncBefore = EBarrierSync::ALL,
						.syncAfter = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::COMMON,
						.accessAfter = EBarrierAccess::UNORDERED_ACCESS,
						.buffer = buffer2.get(),
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
					auto uav = (i == 0) ? buffer1UAV.get() : buffer2UAV.get();

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
						.buffer = buffer1.get(),
					},
					{
						.syncBefore = EBarrierSync::COMPUTE_SHADING,
						.syncAfter = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::UNORDERED_ACCESS,
						.accessAfter = EBarrierAccess::SHADER_RESOURCE,
						.buffer = buffer2.get(),
					},
					{
						.syncBefore = EBarrierSync::NONE,
						.syncAfter = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::NO_ACCESS,
						.accessAfter = EBarrierAccess::UNORDERED_ACCESS,
						.buffer = buffer3.get(),
					},
				};
				commandList->barrier(_countof(barriers), barriers, 0, nullptr, 0, nullptr);
			}
			// Read pass
			{
				auto heap = readPassDescriptor.getDescriptorHeap(0);

				ShaderParameterTable SPT{};
				SPT.structuredBuffer("bufferA", buffer1SRV.get());
				SPT.structuredBuffer("bufferB", buffer2SRV.get());
				SPT.rwBuffer("rwBuffer", buffer3UAV.get());

				commandList->setComputePipelineState(shaders.bufferReadShader.get());
				commandList->bindComputeShaderParameters(shaders.bufferReadShader.get(), &SPT, heap);
				commandList->dispatchCompute(1024, 1, 1);
			}

			commandList->close();
			commandAllocator->markValid();

			commandQueue->executeCommandList(commandList);

			renderDevice->flushCommandQueue();

			// 3. Cleanup

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
		UniquePtr<DescriptorHeap> createDescriptorHeap(RenderDevice* device, EDescriptorHeapType type, uint32 numDescriptors)
		{
			DescriptorHeapDesc desc{
				.type           = type,
				.numDescriptors = numDescriptors,
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

	// #todo-barrier-vk: Enable test for vulkan
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
