#include "static_mesh_rendering.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"

#define kMaxIndirectDrawCommandCount 256

// -----------------------------------------
// GraphicsPipelineKeyDesc

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

// -----------------------------------------
// IndirectDrawHelper

void IndirectDrawHelper::initialize(RenderDevice* inRenderDevice, GraphicsPipelineState* pipelineState, GraphicsPipelineKey inPipelineKey)
{
	device = inRenderDevice;
	pipelineKey = inPipelineKey;

	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	argumentBuffer.initialize(swapchainCount);
	argumentBufferSRV.initialize(swapchainCount);
	culledArgumentBuffer.initialize(swapchainCount);
	culledArgumentBufferUAV.initialize(swapchainCount);
	drawCounterBuffer.initialize(swapchainCount);
	drawCounterBufferUAV.initialize(swapchainCount);

	// Hmm... C++20 designated initializers looks ugly in this case :(
	CommandSignatureDesc commandSignatureDesc{
		.argumentDescs = {
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::CONSTANT,
				.name = "pushConstants",
				.constant = {
					.destOffsetIn32BitValues = 0,
					.num32BitValuesToSet = 1,
				},
			},
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::VERTEX_BUFFER_VIEW,
				.vertexBuffer = {
					.slot = 0, // position buffer slot
				},
			},
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::VERTEX_BUFFER_VIEW,
				.vertexBuffer = {
					.slot = 1, // non-position buffer slot
				},
			},
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::INDEX_BUFFER_VIEW,
			},
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::DRAW_INDEXED,
			},
		},
		.nodeMask = 0,
	};
	commandSignature = UniquePtr<CommandSignature>(
		device->createCommandSignature(commandSignatureDesc, pipelineState));

	argumentBufferGenerator = UniquePtr<IndirectCommandGenerator>(
		device->createIndirectCommandGenerator(commandSignatureDesc, kMaxIndirectDrawCommandCount));

	// Create resources of fixed sizes. Other resources might be reallocated in resizeResources().
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		drawCounterBuffer[i] = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = sizeof(uint32),
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC | EBufferAccessFlags::UAV,
			}
		));

		wchar_t debugName[256];
		swprintf_s(debugName, L"Buffer_IndirectDrawCounterBuffer_%u_%u", pipelineKey, i);
		drawCounterBuffer[i]->setDebugName(debugName);

		UnorderedAccessViewDesc uavDesc{};
		uavDesc.format                      = EPixelFormat::UNKNOWN;
		uavDesc.viewDimension               = EUAVDimension::Buffer;
		uavDesc.buffer.firstElement         = 0;
		uavDesc.buffer.numElements          = 1;
		uavDesc.buffer.structureByteStride  = sizeof(uint32);
		uavDesc.buffer.counterOffsetInBytes = 0;
		uavDesc.buffer.flags                = EBufferUAVFlags::None;

		drawCounterBufferUAV[i] = UniquePtr<UnorderedAccessView>(
			device->createUAV(drawCounterBuffer[i].get(), uavDesc));
	}
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

// -----------------------------------------
// GraphicsPipelineStatePermutation

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
