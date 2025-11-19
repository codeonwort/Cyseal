#include "static_mesh_rendering.h"
#include "rhi/render_device.h"

// -----------------------------------------
// PSO permutation

const GraphicsPipelineKeyDesc GraphicsPipelineKeyDesc::kDefaultPipelineKeyDesc{ ECullMode::Back };
const GraphicsPipelineKeyDesc GraphicsPipelineKeyDesc::kNoCullPipelineKeyDesc{ ECullMode::None };
const GraphicsPipelineKeyDesc GraphicsPipelineKeyDesc::kPipelineKeyDescs[] = {
	kDefaultPipelineKeyDesc, kNoCullPipelineKeyDesc,
};

size_t GraphicsPipelineKeyDesc::numPipelineKeyDescs()
{
	return _countof(kPipelineKeyDescs);
}

GraphicsPipelineKey GraphicsPipelineKeyDesc::assemblePipelineKey(const GraphicsPipelineKeyDesc& desc)
{
	GraphicsPipelineKey key = 0;
	key |= (uint32)(desc.cullMode) - 1;
	return key;
}

void IndirectDrawHelper::resizeResources(uint32 swapchainIndex, uint32 maxDrawCount)
{
	if (argumentBufferGenerator->getMaxCommandCount() < maxDrawCount)
	{
		argumentBufferGenerator->resizeMaxCommandCount(maxDrawCount);
	}

	uint32 requiredCapacity = argumentBufferGenerator->getCommandByteStride() * maxDrawCount;
	Buffer* argBuffer = argumentBuffer.at(swapchainIndex);
	Buffer* culledArgBuffer = culledArgumentBuffer.at(swapchainIndex);

	if (argBuffer == nullptr || argBuffer->getCreateParams().sizeInBytes < requiredCapacity)
	{
		argumentBuffer[swapchainIndex] = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = requiredCapacity,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC | EBufferAccessFlags::UAV,
			}
		));

		// #wip: Parameterize debug name
		wchar_t debugName[256];
		swprintf_s(debugName, L"Buffer_IndirectDrawBuffer_%u_%u", pipelineKey, swapchainIndex);
		argumentBuffer[swapchainIndex]->setDebugName(debugName);

		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                     = EPixelFormat::UNKNOWN;
		srvDesc.viewDimension              = ESRVDimension::Buffer;
		srvDesc.buffer.firstElement        = 0;
		srvDesc.buffer.numElements         = maxDrawCount;
		srvDesc.buffer.structureByteStride = argumentBufferGenerator->getCommandByteStride();
		srvDesc.buffer.flags               = EBufferSRVFlags::None;

		argumentBufferSRV[swapchainIndex] = UniquePtr<ShaderResourceView>(
			device->createSRV(argumentBuffer.at(swapchainIndex), srvDesc));
	}
	if (culledArgBuffer == nullptr || culledArgBuffer->getCreateParams().sizeInBytes < requiredCapacity)
	{
		culledArgumentBuffer[swapchainIndex] = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = requiredCapacity,
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::UAV
			}
		));

		// #wip: Parameterize debug name
		wchar_t debugName[256];
		swprintf_s(debugName, L"Buffer_CulledIndirectDrawBuffer_%u_%u", pipelineKey, swapchainIndex);
		culledArgumentBuffer[swapchainIndex]->setDebugName(debugName);

		UnorderedAccessViewDesc uavDesc{};
		uavDesc.format                      = EPixelFormat::UNKNOWN;
		uavDesc.viewDimension               = EUAVDimension::Buffer;
		uavDesc.buffer.firstElement         = 0;
		uavDesc.buffer.numElements          = maxDrawCount;
		uavDesc.buffer.structureByteStride  = argumentBufferGenerator->getCommandByteStride();
		uavDesc.buffer.counterOffsetInBytes = 0;
		uavDesc.buffer.flags                = EBufferUAVFlags::None;

		culledArgumentBufferUAV[swapchainIndex] = UniquePtr<UnorderedAccessView>(
			device->createUAV(culledArgumentBuffer.at(swapchainIndex), uavDesc));
	}
}

GraphicsPipelineStatePermutation::~GraphicsPipelineStatePermutation()
{
	for (auto& it : pipelines)
	{
		GraphicsPipelineItem item = it.second;
		delete item.pipelineState;
		delete item.indirectDrawHelper;
	}
}

GraphicsPipelineItem GraphicsPipelineStatePermutation::findPipeline(GraphicsPipelineKey key) const
{
	auto it = pipelines.find(key);
	CHECK(it != pipelines.end());
	return it->second;
}

void GraphicsPipelineStatePermutation::insertPipeline(GraphicsPipelineKey key, GraphicsPipelineItem item)
{
	CHECK(false == pipelines.contains(key));
	pipelines.insert(std::make_pair(key, item));
}
