#include "static_mesh_rendering.h"
#include "render/gpu_scene.h"
#include "render/gpu_culling.h"
#include "render/static_mesh.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "world/scene_proxy.h"

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

void IndirectDrawHelper::initialize(
	RenderDevice* inRenderDevice,
	GraphicsPipelineState* pipelineState,
	GraphicsPipelineKey inPipelineKey,
	const wchar_t* inDebugName)
{
	device = inRenderDevice;
	pipelineKey = inPipelineKey;
	CHECK(inDebugName != nullptr);
	debugName = inDebugName;

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

		wchar_t bufferDebugName[256];
		swprintf_s(bufferDebugName, L"Buffer_IndirectDrawCounterBuffer_%s_%u_%u", debugName.c_str(), pipelineKey, i);
		drawCounterBuffer[i]->setDebugName(bufferDebugName);

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

		wchar_t bufferDebugName[256];
		swprintf_s(bufferDebugName, L"Buffer_IndirectDrawBuffer_%s_%u_%u", debugName.c_str(), pipelineKey, swapchainIndex);
		argumentBuffer[swapchainIndex]->setDebugName(bufferDebugName);

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

		wchar_t bufferDebugName[256];
		swprintf_s(bufferDebugName, L"Buffer_CulledIndirectDrawBuffer_%s_%u_%u", debugName.c_str(), pipelineKey, swapchainIndex);
		culledArgumentBuffer[swapchainIndex]->setDebugName(bufferDebugName);

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

// -----------------------------------------
// StaticMeshRendering

VertexInputLayout StaticMeshRendering::createVertexInputLayout()
{
	// #todo-basepass: Should be variant per vertex factory
	return VertexInputLayout {
		{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0},
		{"NORMAL", 0, EPixelFormat::R32G32B32_FLOAT, 1, 0, EVertexInputClassification::PerVertex, 0},
		{"TEXCOORD", 0, EPixelFormat::R32G32_FLOAT, 1, sizeof(float) * 3, EVertexInputClassification::PerVertex, 0}
	};
}

void StaticMeshRendering::renderStaticMeshes(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const StaticMeshRenderingInput& input)
{
	// #todo-renderer: Need smarter way to generate drawlists per pipeline if permutation blows up.
	size_t kNumKeys = GraphicsPipelineKeyDesc::numPipelineKeyDescs();
	std::vector<StaticMeshDrawList> drawsForPipelines(kNumKeys);
	for (size_t i = 0; i < kNumKeys; ++i)
	{
		drawsForPipelines[i].reserve(input.scene->totalMeshSectionsLOD0);
	}
	{
		uint32 objectID = 0;
		for (const StaticMeshProxy* mesh : input.scene->staticMeshes)
		{
			for (const StaticMeshSection& section : mesh->getSections())
			{
				bool bDoubleSided = section.material->bDoubleSided;
				if (bDoubleSided)
				{
					drawsForPipelines[1].meshes.push_back(&section);
					drawsForPipelines[1].objectIDs.push_back(objectID);
				}
				else
				{
					drawsForPipelines[0].meshes.push_back(&section);
					drawsForPipelines[0].objectIDs.push_back(objectID);
				}
				++objectID;
			}
		}
	}

	for (size_t i = 0; i < kNumKeys; ++i)
	{
		const auto& keyDesc = GraphicsPipelineKeyDesc::kPipelineKeyDescs[i];
		const GraphicsPipelineKey key = GraphicsPipelineKeyDesc::assemblePipelineKey(keyDesc);
		renderForPipeline(commandList, swapchainIndex, input, key, drawsForPipelines[i]);
	}
}

void StaticMeshRendering::renderForPipeline(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const StaticMeshRenderingInput& input,
	GraphicsPipelineKey pipelineKey,
	const StaticMeshDrawList& drawList)
{
	auto scene              = input.scene;
	auto camera             = input.camera;
	auto bIndirectDraw      = input.bIndirectDraw;
	auto bGpuCulling        = input.bGpuCulling;
	auto gpuScene           = input.gpuScene;
	auto gpuCulling         = input.gpuCulling;

	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors();

	auto pipelineItem = input.psoPermutation->findPipeline(pipelineKey);
	GraphicsPipelineState* pipelineState = pipelineItem.pipelineState;
	IndirectDrawHelper* indirectDrawHelper = pipelineItem.indirectDrawHelper;

	indirectDrawHelper->resizeResources(swapchainIndex, gpuScene->getGPUSceneItemMaxCount());
	auto argumentBufferGenerator = indirectDrawHelper->argumentBufferGenerator.get();

	uint32 maxIndirectDraws = (uint32)drawList.meshes.size();
	if (maxIndirectDraws == 0)
	{
		return;
	}

	// Fill the indirect draw buffer and perform GPU culling.
	if (bIndirectDraw)
	{
		// #wip: Generate on GPU, not on CPU.
		uint32 indirectCommandID = 0;
		for (size_t i = 0; i < drawList.meshes.size(); ++i)
		{
			const StaticMeshSection* section = drawList.meshes[i];
			const uint32 objectID = drawList.objectIDs[i];

			VertexBuffer* positionBuffer = section->positionBuffer->getGPUResource().get();
			VertexBuffer* nonPositionBuffer = section->nonPositionBuffer->getGPUResource().get();
			IndexBuffer* indexBuffer = section->indexBuffer->getGPUResource().get();

			argumentBufferGenerator->beginCommand(indirectCommandID);

			argumentBufferGenerator->writeConstant32(objectID);
			argumentBufferGenerator->writeVertexBufferView(positionBuffer);
			argumentBufferGenerator->writeVertexBufferView(nonPositionBuffer);
			argumentBufferGenerator->writeIndexBufferView(indexBuffer);
			argumentBufferGenerator->writeDrawIndexedArguments(indexBuffer->getIndexCount(), 1, 0, 0, 0);

			argumentBufferGenerator->endCommand();

			++indirectCommandID;
		}

		Buffer* currentArgumentBuffer = indirectDrawHelper->argumentBuffer.at(swapchainIndex);
		argumentBufferGenerator->copyToBuffer(commandList, maxIndirectDraws, currentArgumentBuffer, 0);

		if (bGpuCulling)
		{
			GPUCullingInput cullingPassInput{
				.camera                      = camera,
				.gpuScene                    = gpuScene,
				.maxDrawCommands             = maxIndirectDraws,
				.indirectDrawBuffer          = currentArgumentBuffer,
				.culledIndirectDrawBuffer    = indirectDrawHelper->culledArgumentBuffer.at(swapchainIndex),
				.drawCounterBuffer           = indirectDrawHelper->drawCounterBuffer.at(swapchainIndex),
				.indirectDrawBufferSRV       = indirectDrawHelper->argumentBufferSRV.at(swapchainIndex),
				.culledIndirectDrawBufferUAV = indirectDrawHelper->culledArgumentBufferUAV.at(swapchainIndex),
				.drawCounterBufferUAV        = indirectDrawHelper->drawCounterBufferUAV.at(swapchainIndex),
			};
			gpuCulling->cullDrawCommands(commandList, swapchainIndex, cullingPassInput);
		}
	}

	commandList->setGraphicsPipelineState(pipelineState);
	commandList->iaSetPrimitiveTopology(kPrimitiveTopology);
	
	if (bIndirectDraw)
	{
		if (bGpuCulling)
		{
			Buffer* argBuffer = indirectDrawHelper->culledArgumentBuffer.at(swapchainIndex);
			Buffer* counterBuffer = indirectDrawHelper->drawCounterBuffer.at(swapchainIndex);
			commandList->executeIndirect(indirectDrawHelper->commandSignature.get(), maxIndirectDraws, argBuffer, 0, counterBuffer, 0);
		}
		else
		{
			Buffer* argBuffer = indirectDrawHelper->argumentBuffer.at(swapchainIndex);
			commandList->executeIndirect(indirectDrawHelper->commandSignature.get(), maxIndirectDraws, argBuffer, 0, nullptr, 0);
		}
	}
	else
	{
		commandList->beginRenderPass();

		for (size_t i = 0; i < drawList.meshes.size(); ++i)
		{
			const StaticMeshSection* section = drawList.meshes[i];
			const uint32 objectID = drawList.objectIDs[i];

			ShaderParameterTable SPT{};
			SPT.pushConstant("pushConstants", objectID);
			commandList->updateGraphicsRootConstants(pipelineState, &SPT);

			VertexBuffer* vertexBuffers[] = {
				section->positionBuffer->getGPUResource().get(),
				section->nonPositionBuffer->getGPUResource().get()
			};
			auto indexBuffer = section->indexBuffer->getGPUResource().get();

			commandList->iaSetVertexBuffers(0, _countof(vertexBuffers), vertexBuffers);
			commandList->iaSetIndexBuffer(indexBuffer);
			commandList->drawIndexedInstanced(indexBuffer->getIndexCount(), 1, 0, 0, 0);
		}

		commandList->endRenderPass();
	}
}
