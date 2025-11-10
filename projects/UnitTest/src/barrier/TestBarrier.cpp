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
		struct BufferTestShaders
		{
			UniquePtr<ComputePipelineState> bufferWriteShader;
			UniquePtr<ComputePipelineState> bufferReadShader;
		};
		struct TextureTestShaders
		{
			UniquePtr<ComputePipelineState> textureWriteShader;
			UniquePtr<ComputePipelineState> textureReadShader;
		};

	// Test methods
	protected:
		void ExecuteBufferBarrier()
		{
			// 1. Initialization

			RenderDevice* renderDevice = createRenderDevice();
			auto uavHeap = createDescriptorHeap(renderDevice, EDescriptorHeapType::UAV, 100);
			auto srvHeap = createDescriptorHeap(renderDevice, EDescriptorHeapType::SRV, 100);
			BufferTestShaders shaders = createBufferTestShaders(renderDevice);

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
						.syncBefore   = EBarrierSync::NONE,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::NO_ACCESS,
						.accessAfter  = EBarrierAccess::UNORDERED_ACCESS,
						.buffer       = buffer1.get(),
					},
					{
						.syncBefore   = EBarrierSync::NONE,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::NO_ACCESS,
						.accessAfter  = EBarrierAccess::UNORDERED_ACCESS,
						.buffer       = buffer2.get(),
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
						.syncBefore   = EBarrierSync::COMPUTE_SHADING,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::UNORDERED_ACCESS,
						.accessAfter  = EBarrierAccess::SHADER_RESOURCE,
						.buffer       = buffer1.get(),
					},
					{
						.syncBefore   = EBarrierSync::COMPUTE_SHADING,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::UNORDERED_ACCESS,
						.accessAfter  = EBarrierAccess::SHADER_RESOURCE,
						.buffer       = buffer2.get(),
					},
					{
						.syncBefore   = EBarrierSync::NONE,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::NO_ACCESS,
						.accessAfter  = EBarrierAccess::UNORDERED_ACCESS,
						.buffer       = buffer3.get(),
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
		void ExecuteTextureBarrier()
		{
			// 1. Initialization

			RenderDevice* renderDevice = createRenderDevice();
			auto uavHeap = createDescriptorHeap(renderDevice, EDescriptorHeapType::UAV, 100);
			auto srvHeap = createDescriptorHeap(renderDevice, EDescriptorHeapType::SRV, 100);
			TextureTestShaders shaders = createTextureTestShaders(renderDevice);

			TextureCreateParams textureParams = TextureCreateParams::texture2D(
				EPixelFormat::R16G16B16A16_FLOAT,
				ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
				1920, 1080, 1);
			auto texture1 = UniquePtr<Texture>(renderDevice->createTexture(textureParams));
			auto texture2 = UniquePtr<Texture>(renderDevice->createTexture(textureParams));

			textureParams.accessFlags |= ETextureAccessFlags::RTV;
			auto texture3 = UniquePtr<Texture>(renderDevice->createTexture(textureParams));
			Assert::IsTrue(texture1 != nullptr, L"Texture is null");
			Assert::IsTrue(texture2 != nullptr, L"Texture is null");
			Assert::IsTrue(texture3 != nullptr, L"Texture is null");

			UnorderedAccessViewDesc uavDesc{
				.format        = textureParams.format,
				.viewDimension = EUAVDimension::Texture2D,
				.texture2D     = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			};
			auto texture1UAV = UniquePtr<UnorderedAccessView>(renderDevice->createUAV(texture1.get(), uavHeap.get(), uavDesc));
			auto texture2UAV = UniquePtr<UnorderedAccessView>(renderDevice->createUAV(texture2.get(), uavHeap.get(), uavDesc));
			auto texture3UAV = UniquePtr<UnorderedAccessView>(renderDevice->createUAV(texture3.get(), uavHeap.get(), uavDesc));

			ShaderResourceViewDesc srvDesc{
				.format        = textureParams.format,
				.viewDimension = ESRVDimension::Texture2D,
				.texture2D     = Texture2DSRVDesc{},
			};
			auto texture1SRV = UniquePtr<ShaderResourceView>(renderDevice->createSRV(texture1.get(), srvHeap.get(), srvDesc));
			auto texture2SRV = UniquePtr<ShaderResourceView>(renderDevice->createSRV(texture2.get(), srvHeap.get(), srvDesc));

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

			// Barrier (initial -> write pass)
			{
				TextureBarrier barriers[] = {
					{
						.syncBefore   = EBarrierSync::NONE,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::NO_ACCESS,
						.accessAfter  = EBarrierAccess::UNORDERED_ACCESS,
						.layoutBefore = EBarrierLayout::Common,
						.layoutAfter  = EBarrierLayout::UnorderedAccess,
						.texture      = texture1.get(),
						.subresources = BarrierSubresourceRange{
							.indexOrFirstMipLevel = 0,
							.numMipLevels         = 1,
							.firstArraySlice      = 0,
							.numArraySlices       = 0,
							.firstPlane           = 0,
							.numPlanes            = 0,
						},
						.flags        = ETextureBarrierFlags::None,
					},
					{
						.syncBefore   = EBarrierSync::NONE,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::NO_ACCESS,
						.accessAfter  = EBarrierAccess::UNORDERED_ACCESS,
						.layoutBefore = EBarrierLayout::Common,
						.layoutAfter  = EBarrierLayout::UnorderedAccess,
						.texture      = texture2.get(),
						.subresources = BarrierSubresourceRange{
							.indexOrFirstMipLevel = 0,
							.numMipLevels         = 1,
							.firstArraySlice      = 0,
							.numArraySlices       = 0,
							.firstPlane           = 0,
							.numPlanes            = 0,
						},
						.flags        = ETextureBarrierFlags::None,
					},
				};
				commandList->barrier(0, nullptr, _countof(barriers), barriers, 0, nullptr);
			}
			// Write pass
			{
				auto heap = writePassDescriptor.getDescriptorHeap(0);
				DescriptorIndexTracker tracker;
				for (uint32 i = 0; i < 2; ++i)
				{
					auto uav = (i == 0) ? texture1UAV.get() : texture2UAV.get();

					ShaderParameterTable SPT{};
					SPT.pushConstants("pushConstants", { textureParams.width, textureParams.height });
					SPT.rwTexture("rwTexture", uav);

					commandList->setComputePipelineState(shaders.textureWriteShader.get());
					commandList->bindComputeShaderParameters(shaders.textureWriteShader.get(), &SPT, heap, &tracker);
					
					uint32 dispatchX = (textureParams.width + 7) / 8;
					uint32 dispatchY = (textureParams.height + 7) / 8;
					commandList->dispatchCompute(dispatchX, dispatchY, 1);
				}
			}
			// Barrier (write pass -> read pass)
			{
				TextureBarrier barriers[] = {
					{
						.syncBefore   = EBarrierSync::COMPUTE_SHADING,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::UNORDERED_ACCESS,
						.accessAfter  = EBarrierAccess::SHADER_RESOURCE,
						.layoutBefore = EBarrierLayout::UnorderedAccess,
						.layoutAfter  = EBarrierLayout::ShaderResource,
						.texture      = texture1.get(),
						.subresources = BarrierSubresourceRange{
							.indexOrFirstMipLevel = 0,
							.numMipLevels         = 1,
							.firstArraySlice      = 0,
							.numArraySlices       = 0,
							.firstPlane           = 0,
							.numPlanes            = 0,
						},
						.flags        = ETextureBarrierFlags::None,
					},
					{
						.syncBefore   = EBarrierSync::COMPUTE_SHADING,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::UNORDERED_ACCESS,
						.accessAfter  = EBarrierAccess::SHADER_RESOURCE,
						.layoutBefore = EBarrierLayout::UnorderedAccess,
						.layoutAfter  = EBarrierLayout::ShaderResource,
						.texture      = texture2.get(),
						.subresources = BarrierSubresourceRange{
							.indexOrFirstMipLevel = 0,
							.numMipLevels         = 1,
							.firstArraySlice      = 0,
							.numArraySlices       = 0,
							.firstPlane           = 0,
							.numPlanes            = 0,
						},
						.flags        = ETextureBarrierFlags::None,
					},
					{
						.syncBefore   = EBarrierSync::NONE,
						.syncAfter    = EBarrierSync::COMPUTE_SHADING,
						.accessBefore = EBarrierAccess::NO_ACCESS,
						.accessAfter  = EBarrierAccess::UNORDERED_ACCESS,
						.layoutBefore = EBarrierLayout::Common,
						.layoutAfter  = EBarrierLayout::UnorderedAccess,
						.texture      = texture3.get(),
						.subresources = BarrierSubresourceRange{
							.indexOrFirstMipLevel = 0,
							.numMipLevels         = 1,
							.firstArraySlice      = 0,
							.numArraySlices       = 0,
							.firstPlane           = 0,
							.numPlanes            = 0,
						},
						.flags        = ETextureBarrierFlags::None,
					},
				};
				commandList->barrier(0, nullptr, _countof(barriers), barriers, 0, nullptr);
			}
			// Read pass
			{
				auto heap = readPassDescriptor.getDescriptorHeap(0);

				ShaderParameterTable SPT{};
				SPT.pushConstants("pushConstants", { textureParams.width, textureParams.height });
				SPT.texture("textureA", texture1SRV.get());
				SPT.texture("textureB", texture2SRV.get());
				SPT.rwTexture("rwTexture", texture3UAV.get());

				commandList->setComputePipelineState(shaders.textureReadShader.get());
				commandList->bindComputeShaderParameters(shaders.textureReadShader.get(), &SPT, heap);

				uint32 dispatchX = (textureParams.width + 7) / 8;
				uint32 dispatchY = (textureParams.height + 7) / 8;
				commandList->dispatchCompute(dispatchX, dispatchY, 1);
			}
			// Barrier (read pass -> present)
			{
				TextureBarrier barriers[] = {
					{
						.syncBefore   = EBarrierSync::COMPUTE_SHADING,
						.syncAfter    = EBarrierSync::ALL, // #todo-barrier: what should syncAfter be for present?
						.accessBefore = EBarrierAccess::UNORDERED_ACCESS,
						.accessAfter  = EBarrierAccess::COMMON,
						.layoutBefore = EBarrierLayout::UnorderedAccess,
						.layoutAfter  = EBarrierLayout::Present,
						.texture      = texture3.get(),
						.subresources = BarrierSubresourceRange{
							.indexOrFirstMipLevel = 0,
							.numMipLevels         = 1,
							.firstArraySlice      = 0,
							.numArraySlices       = 0,
							.firstPlane           = 0,
							.numPlanes            = 0,
						},
						.flags        = ETextureBarrierFlags::None,
					},
				};
				commandList->barrier(0, nullptr, _countof(barriers), barriers, 0, nullptr);
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
				.purpose        = EDescriptorHeapPurpose::Persistent,
			};
			DescriptorHeap* heap = device->createDescriptorHeap(desc);
			return UniquePtr<DescriptorHeap>(heap);
		}
		BufferTestShaders createBufferTestShaders(RenderDevice* device)
		{
			ResourceFinder::get().addBaseDirectory(TEST_SHADERS_DIR);
			BufferTestShaders shaders;
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
		TextureTestShaders createTextureTestShaders(RenderDevice* device)
		{
			ResourceFinder::get().addBaseDirectory(TEST_SHADERS_DIR);
			TextureTestShaders shaders;
			{
				ShaderStage* cs = device->createShader(EShaderStage::COMPUTE_SHADER, "WriteTextureCS");
				cs->declarePushConstants({ { "pushConstants", 2 } });
				cs->loadFromFile(L"texture_test.hlsl", "mainCS", { L"WRITE_PASS" });
				ComputePipelineDesc pipelineDesc{ .cs = cs, .nodeMask = 0 };
				ComputePipelineState* pipeline = device->createComputePipelineState(pipelineDesc);
				CHECK(pipeline != nullptr);
				delete cs;
				shaders.textureWriteShader = UniquePtr<ComputePipelineState>(pipeline);
			}
			{
				ShaderStage* cs = device->createShader(EShaderStage::COMPUTE_SHADER, "ReadTextureCS");
				cs->declarePushConstants({ { "pushConstants", 2 } });
				cs->loadFromFile(L"texture_test.hlsl", "mainCS", { L"READ_PASS" });
				ComputePipelineDesc pipelineDesc{ .cs = cs, .nodeMask = 0 };
				ComputePipelineState* pipeline = device->createComputePipelineState(pipelineDesc);
				CHECK(pipeline != nullptr);
				delete cs;
				shaders.textureReadShader = UniquePtr<ComputePipelineState>(pipeline);
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
		TEST_METHOD(ExecuteTextureBarrier)
		{
			TestBarrierBase::ExecuteTextureBarrier();
		}
	};

	// #wip: Make barrier test pass
	TEST_CLASS(TestBarrierVulkan), TestBarrierBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(ExecuteBufferBarrier)
		{
			TestBarrierBase::ExecuteBufferBarrier();
		}
		TEST_METHOD(ExecuteTextureBarrier)
		{
			TestBarrierBase::ExecuteTextureBarrier();
		}
	};
}
