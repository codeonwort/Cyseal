#include "volatile_descriptor.h"
#include "rhi/render_device.h"
#include "rhi/descriptor_heap.h"

#define UNIFORM_MEMORY_POOL_SIZE (256 * 1024) // 256 KiB

DEFINE_LOG_CATEGORY_STATIC(LogRHI)

void VolatileDescriptorHelper::initialize(RenderDevice* inRenderDevice, const wchar_t* inPassName, uint32 swapchainCount, uint32 uniformTotalSize, uint32 uniformChunkCount)
{
	renderDevice = inRenderDevice;
	passName = inPassName;
	totalDescriptor.resize(swapchainCount, 0);
	descriptorHeap.initialize(swapchainCount);
	chunkCount = uniformChunkCount;

	// Uniforms
	if (uniformTotalSize > 0)
	{
		CHECK(uniformTotalSize * swapchainCount <= UNIFORM_MEMORY_POOL_SIZE);
		CHECK(uniformTotalSize % uniformChunkCount == 0);
		CHECK(uniformChunkCount > 0);

		uniformMemory = UniquePtr<Buffer>(renderDevice->createBuffer(
			BufferCreateParams{
				.sizeInBytes = UNIFORM_MEMORY_POOL_SIZE,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::CPU_WRITE,
			}
		));

		uniformDescriptorHeap = UniquePtr<DescriptorHeap>(renderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV,
				.numDescriptors = swapchainCount * chunkCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
				.purpose        = EDescriptorHeapPurpose::Volatile,
			}
		));

		const uint32 alignment = renderDevice->getConstantBufferDataAlignment();
		uint32 bufferOffset = 0;
		uniformCBVs.initialize(swapchainCount);
		for (uint32 i = 0; i < swapchainCount; ++i)
		{
			uniformCBVs[i].resize(uniformChunkCount);
			for (uint32 j = 0; j < uniformChunkCount; ++j)
			{
				uniformCBVs[i][j] = UniquePtr<ConstantBufferView>(
					renderDevice->createCBV(
						uniformMemory.get(),
						uniformDescriptorHeap.get(),
						uniformTotalSize,
						bufferOffset));

				bufferOffset += Cymath::alignBytes(uniformTotalSize, alignment);
			}
		}
	}
}

void VolatileDescriptorHelper::initialize(const wchar_t* inPassName, uint32 swapchainCount, uint32 uniformTotalSize, uint32 uniformChunkCount)
{
	CHECK(gRenderDevice != nullptr);
	initialize(gRenderDevice, inPassName, swapchainCount, uniformTotalSize, uniformChunkCount);
}

void VolatileDescriptorHelper::destroy()
{
	descriptorHeap.clear();
	uniformMemory.reset();
	uniformDescriptorHeap.reset();
	uniformCBVs.clear();
}

void VolatileDescriptorHelper::resizeDescriptorHeap(uint32 swapchainIndex, uint32 maxDescriptors)
{
	if (maxDescriptors <= totalDescriptor[swapchainIndex])
	{
		return;
	}
	totalDescriptor[swapchainIndex] = maxDescriptors;

	descriptorHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(renderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::CBV_SRV_UAV,
			.numDescriptors = maxDescriptors,
			.flags          = EDescriptorHeapFlags::ShaderVisible,
			.nodeMask       = 0,
			.purpose        = EDescriptorHeapPurpose::Volatile,
		}
	));

	wchar_t debugName[256];
	swprintf_s(debugName, L"%s_VolatileDescriptors_%u", passName.c_str(), swapchainIndex);
	descriptorHeap[swapchainIndex]->setDebugName(debugName);

	CYLOG(LogRHI, Log, L"Resize [%s]: %u descriptors", debugName, maxDescriptors);
}
