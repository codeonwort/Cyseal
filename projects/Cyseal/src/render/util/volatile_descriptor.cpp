#include "volatile_descriptor.h"
#include "rhi/render_device.h"

#define UNIFORM_MEMORY_POOL_SIZE (256 * 1024) // 256 KiB

DEFINE_LOG_CATEGORY_STATIC(LogRHI)

void VolatileDescriptorHelper::initialize(RenderDevice* inRenderDevice, const wchar_t* inPassName, uint32 swapchainCount, uint32 uniformTotalSize)
{
	renderDevice = inRenderDevice;
	passName = inPassName;
	totalDescriptor.resize(swapchainCount, 0);
	descriptorHeap.initialize(swapchainCount);

	// Uniforms
	if (uniformTotalSize > 0)
	{
		CHECK(uniformTotalSize * swapchainCount <= UNIFORM_MEMORY_POOL_SIZE);

		uniformMemory = UniquePtr<Buffer>(renderDevice->createBuffer(
			BufferCreateParams{
				.sizeInBytes = UNIFORM_MEMORY_POOL_SIZE,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC,
			}
		));

		uniformDescriptorHeap = UniquePtr<DescriptorHeap>(renderDevice->createDescriptorHeap(
			DescriptorHeapDesc{
				.type           = EDescriptorHeapType::CBV,
				.numDescriptors = swapchainCount,
				.flags          = EDescriptorHeapFlags::None,
				.nodeMask       = 0,
				.purpose        = EDescriptorHeapPurpose::Volatile,
			}
		));

		uint32 bufferOffset = 0;
		uniformCBVs.initialize(swapchainCount);
		for (uint32 i = 0; i < swapchainCount; ++i)
		{
			uniformCBVs[i] = UniquePtr<ConstantBufferView>(
				renderDevice->createCBV(
					uniformMemory.get(),
					uniformDescriptorHeap.get(),
					uniformTotalSize,
					bufferOffset));

			uint32 alignment = renderDevice->getConstantBufferDataAlignment();
			bufferOffset += Cymath::alignBytes(uniformTotalSize, alignment);
		}
	}
}

void VolatileDescriptorHelper::initialize(const wchar_t* inPassName, uint32 swapchainCount, uint32 uniformTotalSize)
{
	CHECK(gRenderDevice != nullptr);
	initialize(gRenderDevice, inPassName, swapchainCount, uniformTotalSize);
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
